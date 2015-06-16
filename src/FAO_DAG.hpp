//    This file is part of CVXcanon.
//
//    CVXcanon is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    CVXcanon is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with CVXcanon.  If not, see <http://www.gnu.org/licenses/>.

#ifndef FAO_DAG_H
#define FAO_DAG_H

#include "FAO.hpp"
#include <algorithm>
#include <vector>
#include <map>
#include <queue>
#include <functional>
#include "gsl/gsl_vector.h"

class FAO_DAG {
public:
// TODO only expose start_node input type and end_node output type.
/* Represents an FAO DAG. Used to evaluate FAO DAG and its adjoint. */
	FAO* start_node;
	FAO* end_node;
	std::queue<FAO *> ready_queue;
	std::map<FAO *, size_t> eval_map;

	FAO_DAG(FAO* start, FAO* end) {
		printf("FAO_DAG <><><><>\n");
		start_node = start;
		end_node = end;
		/* Allocate input and output arrays on each node. */
		auto node_fn = [](FAO* node) {
			node->alloc_data();
			node->init_offset_maps();
		};
		traverse_graph(node_fn, true);
		printf("finished FAO DAG <><><><>\n");
	}

	~FAO_DAG() {
		/* Deallocate input and output arrays on each node. */
		auto node_fn = [](FAO* node) {node->free_data();};
		traverse_graph(node_fn, true);
		printf("~FAO_DAG <><><>\n");
	}

	void traverse_graph (std::function<void(FAO*)> node_fn, bool forward) {
		/* Traverse the graph and apply the given function at each node.

		   forward: Traverse in standard or reverse order?
		   node_fn: Function to evaluate on each node.
		*/
		FAO *start;
		if (forward) {
			start = start_node;
		} else {
			start = end_node;
		}
		ready_queue.push(start);
		while (!ready_queue.empty()) {
			FAO* curr = ready_queue.front();
			ready_queue.pop();
			// Evaluate the given function on curr.
			node_fn(curr);
			eval_map[curr]++;
			std::vector<FAO *> child_nodes;
			if (forward) {
				child_nodes = curr->output_nodes;
			} else {
				child_nodes = curr->input_nodes;
			}
			/* If each input has visited the node, it is ready. */
			printf("num children=%lu\n", child_nodes.size());
			for (auto node : child_nodes) {
				eval_map[node]++;
				printf("eval_map[node]=%lu\n", eval_map[node]);
				printf("node->input_nodes.size()=%lu\n", node->input_nodes.size());
				printf("node->output_nodes.size()=%lu\n", node->output_nodes.size());
				size_t node_inputs_count;
				if (forward) {
					node_inputs_count = node->input_nodes.size();
				} else {
					node_inputs_count = node->output_nodes.size();
				}
				if (eval_map[node] == node_inputs_count)
					ready_queue.push(node);
			}
		}
		eval_map.clear();
	}

	/* For interacting with Python. */
	void copy_input(std::vector<double>& input, bool forward) {
		gsl::vector<double>* input_vec;
		if (forward) {
			input_vec = get_forward_input();
		} else {
			input_vec = get_adjoint_input();
		}
		assert(input.size() == input_vec->size);
		gsl::vector_memcpy<double>(input_vec, input.data());
	}

	void copy_output(std::vector<double>& output, bool forward) {
		gsl::vector<double>* output_vec;
		if (forward) {
			output_vec = get_forward_output();
		} else {
			output_vec = get_adjoint_output();
		}
		for (size_t i=0; i < output_vec->size; ++i) {
			printf("output_vec[%zu]=%e\n", i, output_vec->data[i]);
		}
		assert(output.size() == output_vec->size);
		gsl::vector_memcpy<double>(output.data(), output_vec);
	}

	/* Returns a pointer to the input vector for forward evaluation. */
	gsl::vector<double>* get_forward_input() {
		return &start_node->input_data;
	}

	/* Returns a pointer to the output vector for forward evaluation. */
	gsl::vector<double>* get_forward_output() {
		return &end_node->output_data;
	}

	/* Returns a pointer to the input vector for forward evaluation. */
	gsl::vector<double>* get_adjoint_input() {
		return get_forward_output();
	}

	/* Returns a pointer to the output vector for forward evaluation. */
	gsl::vector<double>* get_adjoint_output() {
		return get_forward_input();
	}

	/* Evaluate the FAO DAG. */
	void forward_eval() {
		auto node_eval = [](FAO *node){
			for (size_t i=0; i < node->input_data.size; ++i){
				printf("node->input_data[%lu]=%e\n", i, node->input_data.data[i]);
			}
			node->forward_eval(node->input_data, node->output_data);
			for (size_t i=0; i < node->output_data.size; ++i){
				printf("node->output_data[%lu]=%e\n", i, node->output_data.data[i]);
			}
			// Copy data from node to children.
			for (size_t i=0; i < node->output_nodes.size(); ++i) {
				FAO* target = node->output_nodes[i];
				size_t len = node->get_elem_length(node->output_sizes[i]);
				size_t node_offset = node->output_offsets[target];
				size_t target_offset = target->input_offsets[node];
				// Copy len elements from node_start to target_start.
				gsl::vector_subvec_memcpy<double>(&target->input_data, target_offset,
							  &node->output_data, node_offset, len);
			}
		};
		traverse_graph(node_eval, true);
	}

	/* Evaluate the adjoint DAG. */
	void adjoint_eval() {
		auto node_eval = [](FAO *node){
			for (size_t i=0; i < node->output_data.size; ++i){
				printf("node->output_data[%lu]=%e\n", i, node->output_data.data[i]);
			}
			node->adjoint_eval(node->output_data, node->input_data);
			printf("node->is_inplace()=%d\n", node->is_inplace());
			for (size_t i=0; i < node->input_data.size; ++i){
				printf("node->input_data[%lu]=%e\n", i, node->input_data.data[i]);
			}
			// Copy data from node to children.
			for (size_t i=0; i < node->input_nodes.size(); ++i) {
				FAO* target = node->input_nodes[i];
				size_t len = node->get_elem_length(node->input_sizes[i]);
				size_t node_offset = node->input_offsets[target];
				size_t target_offset = target->output_offsets[node];
				printf("i=%lu, node_offset=%lu, target_offset=%lu\n", i, node_offset, target_offset);
				// Copy len elements from node_start to target_start.
				gsl::vector_subvec_memcpy<double>(&target->output_data, target_offset,
							  &node->input_data, node_offset, len);
			}
		};
		printf("adjoint stuff <><><>\n");
		traverse_graph(node_eval, false);
	}
};
#endif
