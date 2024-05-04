#include "threadpool.h"

#include <chrono>
#include <thread>
#include <iostream>

class MyTask : public Task{
    public:
    MyTask(int begin, int end)
        : begin_(begin)
        , end_(end)
    {}
    // 问题1：怎么设计run函数的返回值，可以表示任意的类型
    // C++17 Any类型
    Any run(){

        std::cout << "tid: " << std::this_thread::get_id() << "begin!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));

        unsigned long long sum = 0;
        for (unsigned long long i = begin_; i <= end_; ++i)
            sum += i;
        
        std::cout << "tid: " << std::this_thread::get_id() << "end!" << std::endl;

        return sum;
    }

    private:
        int begin_;
        int end_;
};

int main(){
    {
        ThreadPool pool;
        pool.start(4);
        Result res = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        // unsigned long long sum = res.get().cast_<unsigned long long>();
    
        // std::cout << sum << std::endl;
    }
    
    std::cout << "main over!" << std::endl;

#if 0
    // ThreadPool对象析构以后，如何把线程池相关线程资源回收
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_CACHED);
    pool.start(4);

    Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
    Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
    Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
    pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

    pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
    pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

    unsigned long long sum1 = res1.get().cast_<unsigned long long>();
    unsigned long long sum2 = res2.get().cast_<unsigned long long>();
    unsigned long long sum3 = res3.get().cast_<unsigned long long>();

    // Master - Slave 线程模型
    // Master线程用来分解任务，然构给各个Slave线程分配任务

    std::cout << (sum1 + sum2 + sum3) << std::endl;

    getchar();

    return 0;
#endif
}