#pragma once
#include "kpoolmemmgr.h"
#include <cstddef>
template <typename T>
class DoublyLinkedList {
private:
    struct Node {
        T data;
        Node* prev;
        Node* next;
        
        Node(const T& value) : data(value), prev(nullptr), next(nullptr) {}
    };

    Node* head;
    Node* tail;
    size_t size;

public:
    // 构造函数
    DoublyLinkedList() : head(nullptr), tail(nullptr), size(0) {}
    
    // 析构函数
    ~DoublyLinkedList() {
        clear();
    }
    
    // 拷贝构造函数
    DoublyLinkedList(const DoublyLinkedList& other) : head(nullptr), tail(nullptr), size(0) {
        Node* current = other.head;
        while (current != nullptr) {
            push_back(current->data);
            current = current->next;
        }
    }
    
    // 拷贝赋值运算符
    DoublyLinkedList& operator=(const DoublyLinkedList& other) {
        if (this != &other) {
            clear();
            Node* current = other.head;
            while (current != nullptr) {
                push_back(current->data);
                current = current->next;
            }
        }
        return *this;
    }
    // 在链表头部添加元素
    void push_front(const T& value) {
        Node* newNode = new Node(value);
        if (head == nullptr) {
            head = tail = newNode;
        } else {
            newNode->next = head;
            head->prev = newNode;
            head = newNode;
        }
        size++;
    }
    
    // 在链表尾部添加元素
    void push_back(const T& value) {
        Node* newNode = new Node(value);
        if (tail == nullptr) {
            head = tail = newNode;
        } else {
            tail->next = newNode;
            newNode->prev = tail;
            tail = newNode;
        }
        size++;
    }
    
    // 删除链表头部元素
    void pop_front() {
        if (head == nullptr) return;
        
        Node* temp = head;
        head = head->next;
        if (head != nullptr) {
            head->prev = nullptr;
        } else {
            tail = nullptr;
        }
        delete temp;
        size--;
    }
    
    // 删除链表尾部元素
    void pop_back() {
        if (tail == nullptr) return;
        
        Node* temp = tail;
        tail = tail->prev;
        if (tail != nullptr) {
            tail->next = nullptr;
        } else {
            head = nullptr;
        }
        delete temp;
        size--;
    }
    
    // 在指定位置前插入元素
    void insert_before(Node* node, const T& value) {
        if (node == nullptr) return;
        
        if (node == head) {
            push_front(value);
        } else {
            Node* newNode = new Node(value);
            newNode->prev = node->prev;
            newNode->next = node;
            node->prev->next = newNode;
            node->prev = newNode;
            size++;
        }
    }
    
    // 在指定位置后插入元素
    void insert_after(Node* node, const T& value) {
        if (node == nullptr) return;
        
        if (node == tail) {
            push_back(value);
        } else {
            Node* newNode = new Node(value);
            newNode->prev = node;
            newNode->next = node->next;
            node->next->prev = newNode;
            node->next = newNode;
            size++;
        }
    }
    
    // 删除指定节点
    void erase(Node* node) {
        if (node == nullptr) return;
        
        if (node == head) {
            pop_front();
        } else if (node == tail) {
            pop_back();
        } else {
            node->prev->next = node->next;
            node->next->prev = node->prev;
            delete node;
            size--;
        }
    }
    
    // 清空链表
    void clear() {
        while (head != nullptr) {
            Node* temp = head;
            head = head->next;
            delete temp;
        }
        tail = nullptr;
        size = 0;
    }
    
    // 获取链表大小
    size_t get_size() const {
        return size;
    }
    
    // 检查链表是否为空
    bool empty() const {
        return size == 0;
    }
    
    // 获取头部节点
    Node* front() const {
        return head;
    }
    
    // 获取尾部节点
    Node* back() const {
        return tail;
    }
    
    // 查找元素，返回第一个匹配的节点指针
    Node* find(const T& value) const {
        Node* current = head;
        while (current != nullptr) {
            if (current->data == value) {
                return current;
            }
            current = current->next;
        }
        return nullptr;
    }
    
    // 迭代器支持
    class Iterator {
    private:
        Node* current;
        
    public:
        Iterator(Node* node) : current(node) {}
        
        T& operator*() const {
            return current->data;
        }
        
        T* operator->() const {
            return &current->data;
        }
        
        Iterator& operator++() {
            current = current->next;
            return *this;
        }
        
        Iterator operator++(int) {
            Iterator temp = *this;
            ++(*this);
            return temp;
        }
        
        Iterator& operator--() {
            current = current->prev;
            return *this;
        }
        
        Iterator operator--(int) {
            Iterator temp = *this;
            --(*this);
            return temp;
        }
        
        bool operator==(const Iterator& other) const {
            return current == other.current;
        }
        
        bool operator!=(const Iterator& other) const {
            return !(*this == other);
        }
    };
    
    Iterator begin() const {
        return Iterator(head);
    }
    
    Iterator end() const {
        return Iterator(nullptr);
    }
    
    // 反向迭代器支持
    class ReverseIterator {
    private:
        Node* current;
        
    public:
        ReverseIterator(Node* node) : current(node) {}
        
        T& operator*() const {
            return current->data;
        }
        
        T* operator->() const {
            return &current->data;
        }
        
        ReverseIterator& operator++() {
            current = current->prev;
            return *this;
        }
        
        ReverseIterator operator++(int) {
            ReverseIterator temp = *this;
            ++(*this);
            return temp;
        }
        
        ReverseIterator& operator--() {
            current = current->next;
            return *this;
        }
        
        ReverseIterator operator--(int) {
            ReverseIterator temp = *this;
            --(*this);
            return temp;
        }
        
        bool operator==(const ReverseIterator& other) const {
            return current == other.current;
        }
        
        bool operator!=(const ReverseIterator& other) const {
            return !(*this == other);
        }
    };
    
    ReverseIterator rbegin() const {
        return ReverseIterator(tail);
    }
    
    ReverseIterator rend() const {
        return ReverseIterator(nullptr);
    }
};