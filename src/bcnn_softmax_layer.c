/*
* Copyright (c) 2016 Jean-Noel Braun.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include "bcnn_softmax_layer.h"

#include <bh/bh_mem.h>
#include <bh/bh_string.h>

#include "bcnn_utils.h"
#include "bh_log.h"

int bcnn_add_softmax_layer(bcnn_net *net, char *src_id, char *dst_id) {
    int num_nodes = net->num_nodes + 1;
    int sz, i;
    bcnn_node node = {0};
    bcnn_tensor dst_tensor = {0};

    if (net->num_nodes > 0) {
        int is_src_node_found = 0;
        for (i = net->num_tensors - 1; i >= 0; --i) {
            if (strcmp(net->tensors[i].name, src_id) == 0) {
                bcnn_node_add_input(&node, i);
                is_src_node_found = 1;
                break;
            }
        }
        bh_check(is_src_node_found,
                 "Full-connected layer: invalid input node name %s", src_id);
    } else {
        bcnn_node_add_input(&node, 0);
    }

    // Setup output node
    bcnn_tensor_set_shape(&dst_tensor,
                          net->tensors[node.src[0]].n,  // batch size
                          net->tensors[node.src[0]].c,  // depth
                          net->tensors[node.src[0]].h,  // height
                          net->tensors[node.src[0]].w,  // width
                          1);
    bcnn_tensor_allocate(&dst_tensor);
    bh_strfill(&dst_tensor.name, dst_id);
    // Add node to net
    bcnn_net_add_tensor(net, dst_tensor);
    // Add tensor output index to node
    bcnn_node_add_output(&node, net->num_tensors - 1);

    node.layer = (bcnn_layer *)calloc(1, sizeof(bcnn_layer));
    node.layer->type = SOFTMAX;

    bcnn_net_add_node(net, node);

    bh_log_info("[Softmax] input_shape= %dx%dx%d output_shape= %dx%dx%d",
                net->tensors[node.src[0]].w, net->tensors[node.src[0]].h,
                net->tensors[node.src[0]].c, net->tensors[node.dst[0]].w,
                net->tensors[node.dst[0]].h, net->tensors[node.dst[0]].c);

    return BCNN_SUCCESS;
}

int bcnn_forward_softmax_layer_cpu(bcnn_layer *layer, bcnn_tensor *src_tensor,
                                   bcnn_tensor *dst_tensor) {
    int b, i, batch_size = src_tensor->n;
    int src_size = bcnn_tensor_get_size3d(src_tensor);
    float vmax = -FLT_MAX;
    float sum = 0.0f;

    if (src_tensor->w * src_tensor->h == 1) {
        for (b = 0; b < batch_size; ++b) {
            vmax = -FLT_MAX;
            sum = 0.0f;
            for (i = 0; i < src_size; ++i) {
                if (src_tensor->data[b * src_size + i] > vmax) {
                    vmax = src_tensor->data[b * src_size + i];
                }
            }
            for (i = 0; i < src_size; ++i) {
                sum += (float)exp(src_tensor->data[b * src_size + i] - vmax);
            }
            if (sum) {
                sum = vmax + (float)log(sum);
            } else
                sum = vmax - 100.0f;
            for (i = 0; i < src_size; ++i) {
                dst_tensor->data[b * src_size + i] =
                    (float)exp(src_tensor->data[b * src_size + i] - sum);
            }
        }
    } else {
        for (b = 0; b < batch_size; ++b) {
            for (i = 0; i < src_tensor->w * src_tensor->h; ++i) {
                int c;
                vmax = -FLT_MAX;
                sum = 0.0f;
                for (c = 0; c < src_tensor->c; ++c) {
                    vmax = bh_max(
                        vmax,
                        src_tensor
                            ->data[b * src_size +
                                   c * src_tensor->w * src_tensor->h + i]);
                }
                for (c = 0; c < src_tensor->c; ++c) {
                    sum += (float)exp(
                        src_tensor
                            ->data[b * src_size +
                                   c * src_tensor->w * src_tensor->h + i] -
                        vmax);
                }
                if (sum) {
                    sum = vmax + (float)log(sum);
                } else {
                    sum = vmax - 100.0f;
                }
                for (c = 0; c < src_tensor->c; ++c) {
                    dst_tensor->data[b * src_size +
                                     c * src_tensor->w * src_tensor->h + i] =
                        (float)exp(
                            src_tensor
                                ->data[b * src_size +
                                       c * src_tensor->w * src_tensor->h + i] -
                            sum);
                }
            }
        }
    }
    return BCNN_SUCCESS;
}

int bcnn_backward_softmax_layer_cpu(bcnn_layer *layer, bcnn_tensor *src_tensor,
                                    bcnn_tensor *dst_tensor) {
    int i;
    int sz = bcnn_tensor_size(src_tensor);

    for (i = 0; i < sz; ++i)
        src_tensor->grad_data[i] += dst_tensor->grad_data[i];

    return BCNN_SUCCESS;
}

int bcnn_forward_softmax_layer(bcnn_net *net, bcnn_node *node) {
    bcnn_tensor *src = &net->tensors[node->src[0]];
    bcnn_tensor *dst = &net->tensors[node->dst[0]];
#ifdef BCNN_USE_CUDA
    return bcnn_forward_softmax_layer_gpu(node->layer, src, dst);
#else
    return bcnn_forward_softmax_layer_cpu(node->layer, src, dst);
#endif
}

int bcnn_backward_softmax_layer(bcnn_net *net, bcnn_node *node) {
    bcnn_tensor *src = &net->tensors[node->src[0]];
    bcnn_tensor *dst = &net->tensors[node->dst[0]];
#ifdef BCNN_USE_CUDA
    return bcnn_backward_softmax_layer_gpu(node->layer, src, dst);
#else
    return bcnn_backward_softmax_layer_cpu(node->layer, src, dst);
#endif
}