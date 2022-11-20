#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <solution.h>

struct btree_node;

struct btree {
    struct btree_node *root_node;
    int depth;
    int full_node_splitter;
};

#define SIZEOF_KEY sizeof(int)
#define SIZEOF_EDGE sizeof(struct btree_node *)

#define BTREE_NODE_META_SIZE sizeof(int) * 3

struct btree_node {
    int keys_size;
    int is_leaf;
    int *keys;
    struct btree_node **edges;
};

int _min_keys_size(struct btree *t) {
    return t->full_node_splitter - 1;
}

int _max_keys_size(struct btree *t) {
    return (t->full_node_splitter * 2) - 1;
}

// BTree Nodes memory management

struct btree_node *_allocate_btree_node(int keys_size, int is_leaf) {
    struct btree_node *node = (struct btree_node*) malloc(sizeof(struct btree_node));

    node->keys_size = keys_size;
    node->is_leaf = is_leaf;
    node->keys = (int*) malloc(keys_size * SIZEOF_KEY);
    node->edges = is_leaf ? NULL : (struct btree_node **) malloc((keys_size + 1) * SIZEOF_EDGE);

    return node;
}

void _free_btree_node(struct btree_node *node) {
    free(node->keys);
    if (!node->is_leaf) free(node->edges);

    node->keys_size = 0;
    node->is_leaf = 1;

    free(node);
}

// Debugging

void print_node(struct btree_node *node) {
    printf("-----\\\nnode address: %p\n", (void *) node);
    printf("keys amount: %d\n", node->keys_size);
    for (int i = 0; i < node->keys_size; ++i) {
        printf("%d ", node->keys[i]);
    }
    printf("\nis leaf: %d\n", node->is_leaf);
    if (!node->is_leaf) {
        for (int i = 0; i < node->keys_size + 1; ++i) {
            printf("%p ", (void *) node->edges[i]);
        }
    }
    printf("\n-----/\n");
}

// BTree Nodes operations

struct _find_result {
    int index;
    int is_exact_match;
};

struct _find_result _find_key_index(struct btree_node *node, int key) {
    // Find index of key in keys equal to query.
    // If exact match not found return index of closest element,
    // that is smaller than query.
    struct _find_result result;
    result.index = -1;
    result.is_exact_match = 0;

    for (int i = 0; i < node->keys_size; ++i) {
        if (node->keys[i] <= key) {
            result.index = i;
            if (node->keys[i] == key) {
                result.is_exact_match = 1;
                break;
            }
        }
    }

    return result;
}

struct _node_pair {
    struct btree_node *left;
    struct btree_node *right;
    int middle_key;
};

struct _node_pair _split_node_by_key(struct btree_node *node, int key) {
    struct _find_result index = _find_key_index(node, key);
    if (!index.is_exact_match) {
        printf("Attempted split by key %d, not present in node with address %p", key, (void *) node);
        exit(-1);
    }

    if ( (index.index <= 0) || (index.index >= (node->keys_size - 1)) ) {
        printf("Attempted split by key %d with edge index in node with address %p", key, (void *) node);
        exit(-1);
    }

    int left_size = index.index;
    int right_size = node->keys_size - (left_size + 1);

    struct _node_pair result;
    result.left = _allocate_btree_node(left_size, node->is_leaf);
    result.right = _allocate_btree_node(right_size, node->is_leaf);
    result.middle_key = key;

    memcpy((void *) result.left->keys, (void *) node->keys, left_size * SIZEOF_KEY);
    memcpy((void *) result.right->keys, (void *) (node->keys + (left_size + 1)), right_size * SIZEOF_KEY);

    if (!node->is_leaf) {
        memcpy((void *) result.left->edges, (void *) node->edges, (left_size + 1) * SIZEOF_EDGE);
        memcpy((void *) result.right->edges, (void *) (node->edges + (left_size + 1)), (right_size + 1) * SIZEOF_EDGE);
    }

    return result;
}

struct btree_node *_join_nodes(struct _node_pair nodes_to_join) {
    int joined_keys_size = nodes_to_join.left->keys_size + 1 + nodes_to_join.right->keys_size;
    if (nodes_to_join.left->is_leaf != nodes_to_join.right->is_leaf) {
        printf("One of the node to join is a leaf and other is not a leaf\n");
        printf("Node addresses: %p and %p", (void *) nodes_to_join.left, (void *) nodes_to_join.right);
        exit(-1);
    }
    struct btree_node *result = _allocate_btree_node(joined_keys_size, nodes_to_join.left->is_leaf);

    memcpy(
            (void *) result->keys,
            (void *) nodes_to_join.left->keys,
            nodes_to_join.left->keys_size * SIZEOF_KEY
    );
    result->keys[nodes_to_join.left->keys_size] = nodes_to_join.middle_key;
    memcpy(
            (void *) (result->keys + (nodes_to_join.left->keys_size + 1)),
            (void *) nodes_to_join.right->keys,
            nodes_to_join.right->keys_size * SIZEOF_KEY
    );

    if (!nodes_to_join.left->is_leaf) {
        memcpy(
                (void *) result->edges,
                (void *) nodes_to_join.left->edges,
                (nodes_to_join.left->keys_size + 1) * SIZEOF_EDGE
        );
        result->keys[nodes_to_join.left->keys_size] = nodes_to_join.middle_key;
        memcpy(
                (void *) (result->edges + (nodes_to_join.left->keys_size + 1)),
                (void *) nodes_to_join.right->edges,
                (nodes_to_join.right->keys_size + 1) * SIZEOF_EDGE
        );
    }

    return result;
}

struct _insert_result {
    int index;
    struct btree_node *orphan;
};

struct _insert_result _insert_key_into_node(struct btree_node *node, int key) {
    struct _find_result index = _find_key_index(node, key);

    if (index.is_exact_match) {
        printf("Can't insert key %d into node with address %p. Already exists", key, (void *) node);
        exit(-1);
    }

    struct _insert_result result;

    int new_elem_index = index.index + 1;
    result.index = new_elem_index;
    int new_keys_size = node->keys_size + 1;
    node->keys_size = new_keys_size;

    int *new_keys = malloc(new_keys_size * SIZEOF_KEY);
    for (int i = 0; i < new_keys_size; ++i) {
        new_keys[i] = (i == new_elem_index) ? key : node->keys[(i > new_elem_index) ? i - 1 : i];
    }

    free(node->keys);
    node->keys = new_keys;

    if (!node->is_leaf) {
        result.orphan = node->edges[new_elem_index];
        struct btree_node **new_edges = malloc((new_keys_size + 1) * SIZEOF_EDGE);
        for (int i = 0; i < (new_keys_size + 1); ++i) {
            if (i == new_elem_index || i == new_elem_index + 1) {
                new_edges[i] = NULL;
            } else {
                new_edges[i] = node->edges[(i > new_elem_index) ? i - 1 : i];
            }
        }
        free(node->edges);
        node->edges = new_edges;
    }

    return result;
}

struct _delete_result {
    int key;
    int former_index;
    struct btree_node *left_orphan;
    struct btree_node *right_orphan;
};

struct _delete_result _delete_key_from_node(struct btree_node* node, int key) {
    struct _find_result index = _find_key_index(node, key);

    if (!index.is_exact_match) {
        printf("Can't delete key %d from node with address %p. Not found", key, (void *) node);
        exit(-1);
    }

    struct _delete_result result;
    result.key = key;
    result.former_index = index.index;

    --(node->keys_size);
    for (int i = index.index; i < node->keys_size; ++i) {
        node->keys[i] = node->keys[i + 1];
    }

    if (!node->is_leaf) {
        result.left_orphan = node->edges[index.index];
        result.right_orphan = node->edges[index.index + 1];
        node->edges[index.index] = NULL;
        for (int i = index.index + 1; i < node->keys_size + 1; ++i) {
            node->edges[i] = node->edges[i + 1];
        }
    } else {
        result.left_orphan = NULL;
        result.right_orphan = NULL;
    }

    return result;
}

// BTree memory management

struct btree *btree_alloc(unsigned int L) {
    struct btree *t = (struct btree*) malloc(sizeof(struct btree));

    t->root_node = _allocate_btree_node(0, 1);
    t->depth = 1;
    t->full_node_splitter = L;

    return t;
}

void _btree_free_helper(struct btree_node *node) {
    if (!node->is_leaf) {
        for (int i = 0; i < (node->keys_size + 1); ++i) {
            _btree_free_helper(node->edges[i]);
        }
    }

    _free_btree_node(node);
}

void btree_free(struct btree *t) {
    _btree_free_helper(t->root_node);
    free(t);
}

struct _route_element {
    struct btree_node *node;
    int index;
    int parent_edge_index;
};

struct _route {
    struct _route_element *elements;
    int is_exact_match;
    int length;
};

struct _route _build_route(struct btree *t, int key) {
    struct _route route;
    route.elements = malloc(t->depth * sizeof(struct _route_element));
    route.length = 1;
    route.elements[0].node = t->root_node;
    route.elements[0].parent_edge_index = -1;

    while (route.length < t->depth) {
        struct _route_element *current_element = route.elements + (route.length - 1);
        struct _find_result find_result = _find_key_index(current_element->node, key);
        current_element->index = find_result.index;

        if (find_result.is_exact_match) {
            route.is_exact_match = 1;
            return route;
        }

        struct _route_element *next_element = route.elements + route.length;
        next_element->parent_edge_index = find_result.index + 1;
        next_element->node = current_element->node->edges[next_element->parent_edge_index];
        route.length++;
    }

    struct _route_element *current_element = route.elements + (route.length - 1);
    struct _find_result find_result = _find_key_index(current_element->node, key);
    current_element->index = find_result.index;
    route.is_exact_match = find_result.is_exact_match;

    return route;
}

void _free_route(struct _route route) {
    free(route.elements);
}

void btree_insert(struct btree *t, int key) {
    struct _route route = _build_route(t, key);

    if (route.is_exact_match) {
        _free_route(route);
         return;
    }

    int current_key = key;
    struct btree_node *left_orphan = NULL;
    struct btree_node *right_orphan = NULL;

    for (; route.length > 0; --(route.length)) {
        struct _route_element *current_element = route.elements + (route.length - 1);

        // Node have available space for 1 more key
        if (current_element->node->keys_size < _max_keys_size(t)) {
            struct _insert_result insert_result = _insert_key_into_node(current_element->node, current_key);

            if (!current_element->node->is_leaf) {
                int left_index = insert_result.index;
                int right_index = left_index + 1;

                current_element->node->edges[left_index] = left_orphan;
                current_element->node->edges[right_index] = right_orphan;
            }

            _free_route(route);
            return;
        }

            // Node should be split
        else {
            int split_by_key = current_element->node->keys[t->full_node_splitter];
            struct _node_pair pair = _split_node_by_key(current_element->node, split_by_key);

            struct btree_node *node_with_current_key;
            if (current_key < pair.middle_key) {
                node_with_current_key = pair.left;
            } else {
                node_with_current_key = pair.right;
            }

            struct _insert_result insert_result = _insert_key_into_node(node_with_current_key, current_key);
            if (!node_with_current_key->is_leaf) {
                int left_index = insert_result.index;
                int right_index = left_index + 1;

                node_with_current_key->edges[left_index] = left_orphan;
                node_with_current_key->edges[right_index] = right_orphan;
            }

            _free_btree_node(current_element->node);

            left_orphan = pair.left;
            right_orphan = pair.right;
            current_key = pair.middle_key;
        }
    }

    // Getting to this point means root node was split
    struct btree_node *new_root_node = _allocate_btree_node(1, 0);
    new_root_node->keys[0] = current_key;
    new_root_node->edges[0] = left_orphan;
    new_root_node->edges[1] = right_orphan;

    t->root_node = new_root_node;
    ++(t->depth);
    _free_route(route);
}

void _rotate_right(struct btree_node *left, struct btree_node *right, struct btree_node *parent, int old_middle_index) {
    int new_middle_key = left->keys[left->keys_size - 1];
    struct _delete_result delete_result = _delete_key_from_node(left, new_middle_key);

    if (!left->is_leaf) left->edges[left->keys_size] = delete_result.left_orphan;

    int old_middle_key = parent->keys[old_middle_index];
    parent->keys[old_middle_index] = new_middle_key;

    struct _insert_result insert_result = _insert_key_into_node(right, old_middle_key);
    if (!right->is_leaf) {
        right->edges[0] = delete_result.right_orphan;
        right->edges[1] = insert_result.orphan;
    }
}

void _rotate_left(struct btree_node *left, struct btree_node *right, struct btree_node *parent, int old_middle_index) {
    int new_middle_key = right->keys[0];
    struct _delete_result delete_result = _delete_key_from_node(right, new_middle_key);

    if (!right->is_leaf) right->edges[0] = delete_result.right_orphan;

    int old_middle_key = parent->keys[old_middle_index];
    parent->keys[old_middle_index] = new_middle_key;

    struct _insert_result insert_result = _insert_key_into_node(left, old_middle_key);

    if (!left->is_leaf) {
        left->edges[left->keys_size] = delete_result.left_orphan;
        left->edges[left->keys_size - 1] = insert_result.orphan;
    }
}

struct btree_node *_merge(struct btree_node *left, struct btree_node *right, struct btree_node *parent, int middle_index) {
    int middle_key = parent->keys[middle_index];
    struct _delete_result delete_result = _delete_key_from_node(parent, middle_key);

    struct _node_pair nodes_to_join;
    nodes_to_join.left = left;
    nodes_to_join.right = right;
    nodes_to_join.middle_key = middle_key;
    struct btree_node *merged = _join_nodes(nodes_to_join);

    parent->edges[delete_result.former_index] = merged;
    _free_btree_node(left);
    _free_btree_node(right);
    return merged;
}

void btree_delete(struct btree *t, int key) {
    if (!btree_contains(t, key)) {
        return;
    }

    struct btree_node *current_node = t->root_node;
    int current_key = key;

    struct btree_node *next = NULL;
    struct btree_node *left = NULL;
    struct btree_node *right = NULL;

    for (int i = 0; i < (t->depth - 1); ++i) {
        struct _find_result find_result = _find_key_index(current_node, current_key);

        if (!find_result.is_exact_match) {
            next = current_node->edges[find_result.index + 1];
            if (next->keys_size > _min_keys_size(t)) {
                current_node = next;
                continue;
            } else {
                // Init left and right
                if (find_result.index >= 0) {
                    left = current_node->edges[find_result.index];
                } else {
                    left = NULL;
                }
                if (find_result.index < current_node->keys_size - 1) {
                    right = current_node->edges[find_result.index + 2];
                } else {
                    right = NULL;
                }

                if (left != NULL && left->keys_size > _min_keys_size(t)) {
                    _rotate_right(left, next, current_node, find_result.index);
                    current_node = next;
                    continue;
                } else if (right != NULL && right->keys_size > _min_keys_size(t)) {
                    _rotate_left(next, right, current_node, find_result.index + 1);
                    current_node = next;
                    continue;
                } else {
                    struct btree_node* merged = NULL;
                    if (left != NULL) {
                        merged = _merge(left, next, current_node, find_result.index);
                    } else {
                        merged = _merge(next, right, current_node, find_result.index + 1);
                    }

                    // Check if current_node is now empty (Happens only at root)
                    if (current_node->keys_size == 0) {
                        --i;
                        --(t->depth);
                        _free_btree_node(current_node);
                        t->root_node = merged;
                    }
                    current_node = merged;
                    continue;
                }
            }
        }

        else {
            left = current_node->edges[find_result.index];
            right = current_node->edges[find_result.index + 1];

            // Try using closest to the left
            next = left;
            if (next->keys_size >_min_keys_size(t)) {
                while(!next->is_leaf) {
                    next = next->edges[next->keys_size];
                }
                current_key = next->keys[next->keys_size - 1];
                current_node->keys[find_result.index] = current_key;
                current_node = left;
                continue;
            }

            // Try using closest to the left
            next = right;
            if (next->keys_size >_min_keys_size(t)) {
                while(!next->is_leaf) {
                    next = next->edges[0];
                }
                current_key = next->keys[0];
                current_node->keys[find_result.index] = current_key;
                current_node = right;
                continue;
            }

            struct btree_node* merged = _merge(left, right, current_node, find_result.index);

            // Check if current_node is now empty (Happens only at root)
            if (current_node->keys_size == 0) {
                --i;
                _free_btree_node(current_node);
                t->root_node = merged;
            }
            current_node = merged;
            continue;
        }
    }

    _delete_key_from_node(current_node, current_key);
}

bool btree_contains(struct btree *t, int key) {
    struct _route route = _build_route(t, key);
    _free_route(route);
    return route.is_exact_match;
}

struct btree_iter {
    int index_depth;
    int *indexes;
    int is_finished;
    struct btree_node **nodes;
    struct btree* tree;
};

struct btree_iter *btree_iter_start(struct btree *t) {
    struct btree_iter *iter = (struct btree_iter *) malloc(sizeof(struct btree_iter));
    iter->tree = t;
    iter->is_finished = 0;

    iter->indexes = (int *) malloc(t->depth * sizeof(int));
    for (int i = 0; i < t->depth; ++i) {
        iter->indexes[i] = 0;
    }
    iter->index_depth = t->depth - 1;

    iter->nodes = (struct btree_node **) malloc(t->depth * SIZEOF_EDGE);
    iter->nodes[0] = t->root_node;
    for (int i = 1; i < t->depth; ++i) {
        iter->nodes[i] = iter->nodes[i - 1]->edges[0];
    }
    return iter;
}

void btree_iter_end(struct btree_iter *i) {
    free(i->indexes);
    free(i->nodes);
    free(i);
}

bool btree_iter_next(struct btree_iter *i, int *x) {
    if (i->is_finished) {
        return false;
    }

    struct btree_node *current_node = i->nodes[i->index_depth];

    *(x) = current_node->keys[i->indexes[i->index_depth]];
    ++(i->indexes[i->index_depth]);

    if (current_node->is_leaf) {
        while (true) {
            if (i->indexes[i->index_depth] < i->nodes[i->index_depth]->keys_size) {
                return true;
            } else if (i->index_depth == 0) {
                i->is_finished = 1;
                return true;
            } else {
                i->indexes[i->index_depth] = -1;
                --(i->index_depth);
            }
        }
    } else {
        i->nodes[i->index_depth + 1] = i->nodes[i->index_depth]->edges[i->indexes[i->index_depth]];
        i->indexes[i->index_depth + 1] = 0;
        ++(i->index_depth);
        while (i->index_depth < (i->tree->depth - 1)){
            i->nodes[i->index_depth + 1] = i->nodes[i->index_depth]->edges[0];
            i->indexes[i->index_depth + 1] = 0;
            ++(i->index_depth);
        }
        return true;
    }
}
