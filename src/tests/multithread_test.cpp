#include <ktime.h>
#include <pthread.h>
#include <algorithm>
#include <random>
#include <memory>
#include <cstdio>
#include <string_view>
#include <util/OS_utils.h>
#include "util/Ktemplats.h"
#include "util/lock.h"

constexpr uint32_t arr_count = 0x1000;
uint64_t shuffled_arr[arr_count];
uint32_t index=0;
spinlock_cpp_t shuffled_lock;
constexpr uint32_t ring_max_len = 0x400;
class ring{
    private:
    uint64_t ring_buffer[ring_max_len];
    uint64_t head;
    uint64_t tail;
    uint64_t lose_package_count;
    spinlock_cpp_t lock;
    public:
    void print_lose_package_count(){
        lock.lock();
        printf("lose_package_count = %lu\n", lose_package_count);
        lock.unlock();
    }
    void element_add(uint64_t element);
    uint64_t pop_head();//返回~0代表失败
    ring();
}ring_buffer;
constexpr uint32_t Coefficient = 0x10;
Ktemplats::kernel_bitmap results_bitmap(Coefficient*arr_count);//引索代表值
spinlock_cpp_t bitmap_lock;
uint8_t start_line=0;
constexpr uint8_t producer_thread_count=8;
u8ka finished_producer_thread_count(0);
constexpr uint8_t consumer_thread_count=1;
void* producer(void*arg){
    uint8_t assigned_id = *(uint8_t*)arg;
    while(__atomic_load_n(&start_line,__ATOMIC_ACQUIRE)==0);
    for(uint64_t i=0;
        i*producer_thread_count+assigned_id<arr_count;
        i++){
            ring_buffer.element_add(shuffled_arr[i*producer_thread_count+assigned_id]);
        
        }
    ++finished_producer_thread_count;
    return nullptr;
}
uint64_t actual_answer_second[arr_count]={0};
void* consumer(void*arg){
    while(__atomic_load_n(&start_line,__ATOMIC_ACQUIRE)==0);
    uint64_t element=0;
    while(true)
    {
        element=ring_buffer.pop_head();
        if(element==~0ULL){
            if(finished_producer_thread_count.load(atomic_memory_order::acquire)==producer_thread_count){  
                break;
            }
            continue;
        }
        bitmap_lock.lock();
        //actual_answer_second[element]=element;
        results_bitmap.bit_set(element,true);
        if(finished_producer_thread_count.load(atomic_memory_order::release)==producer_thread_count){
            bitmap_lock.unlock();    
            break;
        }
        
        bitmap_lock.unlock();
    }
    return nullptr;
}

int main(int argc, char** argv)
{
    constexpr uint32_t value_space = arr_count * Coefficient;
    std::unique_ptr<uint64_t[]> standard_answer(new (std::nothrow) uint64_t[arr_count]);
    std::unique_ptr<uint64_t[]> candidate_pool(new (std::nothrow) uint64_t[value_space]);
    std::unique_ptr<uint64_t[]> actual_answer(new (std::nothrow) uint64_t[arr_count]);
    if (!standard_answer || !candidate_pool || !actual_answer) {
        return -1;
    }
    start_line=0;
    bool natural_number_dataset = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--natural") {
            natural_number_dataset = true;
            break;
        }
    }

    std::random_device rd;
    std::mt19937_64 rng(rd());

    // 1) 生成测试集：
    //    a) 默认：从 [0, arr_count*Coefficient) 中采样 arr_count 个互异值
    //    b) --natural：由 [0, arr_count) 的自然数全集打乱得到
    if (natural_number_dataset) {
        for (uint32_t i = 0; i < arr_count; ++i) {
            shuffled_arr[i] = i;
        }
        for (uint32_t i = 0; i < arr_count; ++i) {
            std::uniform_int_distribution<uint32_t> dist(i, arr_count - 1);
            const uint32_t j = dist(rng);
            const uint64_t t = shuffled_arr[i];
            shuffled_arr[i] = shuffled_arr[j];
            shuffled_arr[j] = t;
        }
    } else {
        for (uint32_t i = 0; i < value_space; ++i) {
            candidate_pool[i] = i;
        }
        for (uint32_t i = 0; i < arr_count; ++i) {
            std::uniform_int_distribution<uint32_t> dist(i, value_space - 1);
            const uint32_t j = dist(rng);
            const uint64_t t = candidate_pool[i];
            candidate_pool[i] = candidate_pool[j];
            candidate_pool[j] = t;
            shuffled_arr[i] = candidate_pool[i];
        }
    }

    // 2) 主线程排序得到标准答案
    for (uint32_t i = 0; i < arr_count; ++i) {
        standard_answer[i] = shuffled_arr[i];
    }
    std::sort(standard_answer.get(), standard_answer.get() + arr_count);
    constexpr uint8_t cumsurmer_count = 8;
    constexpr uint8_t producer_count = 8;
    pthread_t producers[producer_count];
    pthread_t consumers[cumsurmer_count];
    
    for(uint8_t i = 0; i < producer_count; ++i){
        pthread_create(&producers[i], nullptr, producer, &i);
    }
    for(uint8_t i = 0; i < cumsurmer_count; ++i){
        pthread_create(&consumers[i], nullptr, consumer, nullptr);
    }
    __atomic_store_n(&start_line, 1, __ATOMIC_RELEASE);
    for(uint8_t i = 0; i < producer_count; ++i){
        pthread_join(producers[i], nullptr);
    }
    for(uint8_t i = 0; i < cumsurmer_count; ++i){
        pthread_join(consumers[i], nullptr);
    }
    // 5) 校验结果：results_bitmap 的第 i 位为 1 代表值 i 存在
    uint32_t actual_count = 0;
    for (uint32_t i = 0; i < value_space; ++i) {
        if (results_bitmap.bit_get(i)) {
            if(actual_count>=arr_count){
                printf("actual_count = %u, expected = %u\n", actual_count, arr_count);
                return -1;
            }
            actual_answer[actual_count] = i;
            actual_count++;
        }
    }
    if (actual_count != arr_count) {
        ring_buffer.print_lose_package_count();
        printf("actual_count = %u, expected = %u\n", actual_count, arr_count);
        return -1;
    }
    for (uint32_t i = 0; i < arr_count; ++i) {
        if (actual_answer[i] != standard_answer[i]){
            if(natural_number_dataset){
            for(uint32_t j=0;j<arr_count;++j){
                if(actual_answer_second[j]!=standard_answer[j])
                    printf("actual_answer_second[%u] = %lu, standard_answer[%u] = %lu\n", j, actual_answer_second[j], j, standard_answer[j]);
            }
            printf("ring_test_fail\n");
            return -1;
        }else{
            printf("actual_answer[%u] = %lu, standard_answer[%u] = %lu\n", i, actual_answer[i], i, standard_answer[i]);
            printf("actual_answer_second[%u] = %lu\n", i, actual_answer_second[i]); 
            return -1;
        }
        }
        
    }
    printf("ring_test_pass\n");
    return 0;
}

void ring::element_add(uint64_t element)
{
    this->lock.lock();
    if(((tail+1)%ring_max_len)==head){
        lose_package_count++;
        lock.unlock();
        return;
    }
    ring_buffer[tail]=element;
    tail=(tail+1)%ring_max_len;
    lock.unlock();
}

uint64_t ring::pop_head()
{
    lock.lock();
    if(head==tail){
        lock.unlock();
        return ~0ULL;
    }
    uint64_t result=ring_buffer[head];
    head=((head+1)%ring_max_len);
    lock.unlock();
    return result;
}

ring::ring()
{
    ksetmem_8(this,0,sizeof(ring));    
}
