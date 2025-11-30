#include <util/RB_btree.h>
#include <new>

// --- rotations ---
Node* RBTree_t::left_rotate(Node* x) {
    Node* y = x->right;
    x->right = y->left;
    if (y->left) y->left->parent = x;
    y->left = x;

    y->parent = x->parent;
    x->parent = y;

    return y;
}

Node* RBTree_t::right_rotate(Node* x) {
    Node* y = x->left;
    x->left = y->right;
    if (y->right) y->right->parent = x;
    y->right = x;

    y->parent = x->parent;
    x->parent = y;

    return y;
}

// --- fix insert ---
void RBTree_t::fix_insert(Node* n) {
    while (n != root && n->parent->color == 1) {
        Node* p = n->parent;
        Node* g = p->parent;

        if (p == g->left) {
            Node* u = g->right; // uncle
            if (u && u->color == 1) {
                // case 1: recolor
                p->color = 0;
                u->color = 0;
                g->color = 1;
                n = g;
            } else {
                // case 2/3
                if (n == p->right) {
                    n = p;
                    root = (p->parent ?
                        (p->parent->left == p ?
                            (p->parent->left = left_rotate(p)) :
                            (p->parent->right = left_rotate(p)))
                        : left_rotate(p));
                }
                p = n->parent;
                g = p->parent;
                p->color = 0;
                g->color = 1;

                root = (g->parent ?
                    (g->parent->left == g ?
                        (g->parent->left = right_rotate(g)) :
                        (g->parent->right = right_rotate(g)))
                    : right_rotate(g));
            }
        } else {
            // symmetric
            Node* u = g->left;
            if (u && u->color == 1) {
                p->color = 0;
                u->color = 0;
                g->color = 1;
                n = g;
            } else {
                if (n == p->left) {
                    n = p;
                    root = (p->parent ?
                        (p->parent->left == p ?
                            (p->parent->left = right_rotate(p)) :
                            (p->parent->right = right_rotate(p)))
                        : right_rotate(p));
                }
                p = n->parent;
                g = p->parent;
                p->color = 0;
                g->color = 1;

                root = (g->parent ?
                    (g->parent->left == g ?
                        (g->parent->left = left_rotate(g)) :
                        (g->parent->right = left_rotate(g)))
                    : left_rotate(g));
            }
        }
    }
    root->color = 0; // root is always black
}

// --- find replacement for remove ---
Node* RBTree_t::subtree_min(Node* x) {
    while (x->left) x = x->left;
    return x;
}

void RBTree_t::fix_remove(Node* x, Node* parent) {
    while (x != root && (!x || x->color == 0)) {
        if (x == parent->left) {
            Node* w = parent->right;
            if (w->color == 1) {
                w->color = 0;
                parent->color = 1;
                root = (parent->parent ?
                    (parent->parent->left == parent ?
                        (parent->parent->left = left_rotate(parent)) :
                        (parent->parent->right = left_rotate(parent)))
                    : left_rotate(parent));
                w = parent->right;
            }
            if ((!w->left || w->left->color == 0) &&
                (!w->right || w->right->color == 0)) {
                w->color = 1;
                x = parent;
                parent = x->parent;
            } else {
                if (!w->right || w->right->color == 0) {
                    if (w->left) w->left->color = 0;
                    w->color = 1;

                    root = (w->parent ?
                        (w->parent->left == w ?
                            (w->parent->left = right_rotate(w)) :
                            (w->parent->right = right_rotate(w)))
                        : right_rotate(w));

                    w = parent->right;
                }
                w->color = parent->color;
                parent->color = 0;
                if (w->right) w->right->color = 0;

                root = (parent->parent ?
                    (parent->parent->left == parent ?
                        (parent->parent->left = left_rotate(parent)) :
                        (parent->parent->right = left_rotate(parent)))
                    : left_rotate(parent));

                x = root;
                break;
            }
        } else {
            // symmetric
            Node* w = parent->left;
            if (w->color == 1) {
                w->color = 0;
                parent->color = 1;
                root = (parent->parent ?
                    (parent->parent->left == parent ?
                        (parent->parent->left = right_rotate(parent)) :
                        (parent->parent->right = right_rotate(parent)))
                    : right_rotate(parent));
                w = parent->left;
            }
            if ((!w->right || w->right->color == 0) &&
                (!w->left || w->left->color == 0)) {
                w->color = 1;
                x = parent;
                parent = x->parent;
            } else {
                if (!w->left || w->left->color == 0) {
                    if (w->right) w->right->color = 0;
                    w->color = 1;

                    root = (w->parent ?
                        (w->parent->left == w ?
                            (w->parent->left = left_rotate(w)) :
                            (w->parent->right = left_rotate(w)))
                        : left_rotate(w));

                    w = parent->left;
                }
                w->color = parent->color;
                parent->color = 0;
                if (w->left) w->left->color = 0;

                root = (parent->parent ?
                    (parent->parent->left == parent ?
                        (parent->parent->left = right_rotate(parent)) :
                        (parent->parent->right = right_rotate(parent)))
                    : right_rotate(parent));

                x = root;
                break;
            }
        }
    }

    if (x) x->color = 0;
}

Node* RBTree_t::subtree_max(Node* x)
{
    while (x && x->right)
    x = x->right;
    return x;
} // constructors and public methods
Node* RBTree_t::successor(Node* x) {
    if (!x) return nullptr;

    // case 1: has right subtree
    if (x->right) {
        Node* cur = x->right;
        while (cur->left)
            cur = cur->left;
        return cur;
    }

    // case 2: go up
    Node* p = x->parent;
    while (p && x == p->right) {
        x = p;
        p = p->parent;
    }
    return p;
}

RBTree_t::RBTree_t(int (*cmp_func)(const void*, const void*))
    : root(nullptr), cmp(cmp_func) {}

Node* RBTree_t::search(void* key) {
    Node* cur = root;
    while (cur) {
        int r = cmp(key, cur->data);
        if (r == 0) return cur;
        cur = (r < 0 ? cur->left : cur->right);
    }
    return nullptr;
}

int RBTree_t::insert(void* data) {
    if (!root) {
        root = new Node{data, nullptr, nullptr, nullptr, 0};
        return 0;
    }

    Node* p = root;
    Node* parent = nullptr;

    while (p) {
        parent = p;
        int r = cmp(data, p->data);
        if (r == 0) return -1; // duplicate
        p = (r < 0 ? p->left : p->right);
    }

    Node* n = new Node{data, nullptr, nullptr, parent, 1};

    if (cmp(data, parent->data) < 0)
        parent->left = n;
    else
        parent->right = n;

    fix_insert(n);
    return 0;
}

int RBTree_t::remove(void* key) {
    Node* z = search(key);
    if (!z) return -1;

    Node* y = z;
    Node* x = nullptr;
    Node* x_parent = nullptr;

    bool y_orig_color = y->color;

    if (!z->left) {
        x = z->right;
        x_parent = z->parent;

        if (!z->parent) root = z->right;
        else if (z == z->parent->left) z->parent->left = z->right;
        else z->parent->right = z->right;
        if (z->right) z->right->parent = z->parent;

    } else if (!z->right) {
        x = z->left;
        x_parent = z->parent;

        if (!z->parent) root = z->left;
        else if (z == z->parent->left) z->parent->left = z->left;
        else z->parent->right = z->left;
        if (z->left) z->left->parent = z->parent;

    } else {
        y = subtree_min(z->right);
        y_orig_color = y->color;
        x = y->right;

        if (y->parent == z) {
            x_parent = y;
        } else {
            if (y->right) y->right->parent = y->parent;
            y->parent->right = y->right;

            y->right = z->right;
            y->right->parent = y;
            x_parent = y->parent;
        }

        if (!z->parent) root = y;
        else if (z == z->parent->left) z->parent->left = y;
        else z->parent->right = y;

        y->parent = z->parent;
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    if (y_orig_color == 0)
        fix_remove(x, x_parent);

    delete z;
    return 0;
}