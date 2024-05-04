#include "threadpool.h"

#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10;

ThreadPool::ThreadPool()
    : initThreadSize_(4)
    , taskSize_(0)
    , taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
    , poolMode_(PoolMode::MODE_FIXED)
    , isPoolRunning_(false)
    , idleThreadSize_(0)
    , threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
    , curThreadSize_(0)
{}

// 析构函数
ThreadPool::~ThreadPool(){
    isPoolRunning_ = false;

    // 等待线程池里面所有线程返回  有两种状态（阻塞  &  正在执行中）
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();

    exitCond_.wait(lock, [&]() -> bool
                   { return threads_.size() == 0; });

}

// 设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold){
    if(checkRunningState())
        return;
    taskQueMaxThreshHold_ = threshhold;
}

void ThreadPool::setThreadSizeThreshHold(int threshhold){
    if(checkRunningState())
        return;
    if(poolMode_ == PoolMode::MODE_CACHED)
        threadSizeThreshHold_ = threshhold;
}

// 给线程池提交任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp){
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);

    // 线程的通信 等待任务队列有空余
    // 用户提交任务最长不能阻塞超过1s，否则提交失败
    if(!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool
                  { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
    {
        // 表示notFull_等待1s，条件依然没有满足
        std::cerr << "task queue is full, submit task fail." << std::endl;
                        
        // return task->getResult();        // Task Result task执行完就会被析构
        return Result(sp, false);
    }

    // 如果有空余，把任务放入任务队列中
    taskQue_.emplace(sp);
    taskSize_++;

    // 因为新放入了任务，任务队列肯定不空，在notEmpty_上进行通知
    notEmpty_.notify_all();

    if(poolMode_ == PoolMode::MODE_CACHED
        && taskSize_ > idleThreadSize_
        && curThreadSize_ < threadSizeThreshHold_){
            //创建新线程
            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));

            std::cout << ">>>> create new thread !" << std::endl;

            // threads_.emplace_back(ptr);
            int threadId = ptr->getId();
            threads_.emplace(threadId, std::move(ptr));

            threads_[threadId]->start();            // 启动线程
            curThreadSize_++;                       // 修改线程相关变量
            idleThreadSize_++;
    }

    // 返回任务的Result对象
    // return task->getResult();
    return Result(sp);
}

// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode){
    if(checkRunningState())
        return;
    poolMode_ = mode;
}

bool ThreadPool::checkRunningState() const{
    return isPoolRunning_;
}

// 开启线程池
void ThreadPool::start(int initThreadSize){
    // 设置线程池的运行状态
    isPoolRunning_ = true;

    // 记录初始线程个数
    initThreadSize_ = initThreadSize;
    curThreadSize_ = initThreadSize;

    // 创建线程对象
    for (int i = 0; i < initThreadSize_; ++i){
        // 创建thread线程对象的时候，把线程函数给到thread对象
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        // threads_.emplace_back(std::move(ptr));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
    }

    // 启动所有线程
    for (int i = 0; i < initThreadSize_; ++i){
        threads_[i]->start();           // 执行一个线程函数
        idleThreadSize_++;              // 记录初始空闲线程数量
    }
}

// 定义线程函数，线程池的所有线程从任务队列里面消费任务 
void ThreadPool::threadFunc(int threadid){     // 

    auto lastTime = std::chrono::high_resolution_clock().now();

    // while (isPoolRunning_){
    for (;;){
        std::shared_ptr<Task> task;    
        {
            // 先获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);
            std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务..." << std::endl;

            // 结束回收掉（超过initThreadSize_数量的线程要进行回收）
            // 当前时间 - 上一次线程执行的时间 》 10s

            // 每一秒种返回一次 怎么区分：超时返回？还是有任务待执行返回
            // while(isPoolRunning_ && taskQue_.size() == 0 ){
            while( taskQue_.size() == 0 ){

                if (!isPoolRunning_){
                    threads_.erase(threadid);
                    std::cout << "threadid:" << std::this_thread::get_id() << " exit" << std::endl;
                    exitCond_.notify_all();
                    return;
                }

                if (poolMode_ == PoolMode::MODE_CACHED)
                {
                    // 条件变量 超时返回
                    if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
                        {
                            // 开始回收线程
                            // 记录线程数量的值需要修改
                            // 把线程对象从线程列表容器中删除 没办法threadFunc <----> thread对象
                            // threadId => 线程对象
                            threads_.erase(threadid);
                            curThreadSize_--;
                            idleThreadSize_--;

                            std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
                            return;
                        }
                    }
                }
                else{
                    // 等待notEmpty条件
                    notEmpty_.wait(lock);
                }
                // 线程池结束，要回收资源
                // if(!isPoolRunning_){
                //     threads_.erase(threadid);
                //     std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
                //     exitCond_.notify_all();
                //     return;
                // }
            }
            // if(!isPoolRunning_){
            //     break;
            // }

            std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功..." << std::endl;

            idleThreadSize_--;          // 空闲线程减少

            // 从任务队列中取出一个任务出来
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;

            // 如果依然有剩余任务，继续通知其他线程执行任务
            if(taskQue_.size() > 0){
                notEmpty_.notify_all();
            }
            // 取出一个任务，进行通知，通知可以继续提交生产任务
            notFull_.notify_all();

        } // 出作用域便释放任务队列的锁

        // 当前线程负责执行这个任务
        if(task != nullptr){
            // task->run();            // 执行任务，并将结果返回出来
            task->exec();
        }
        lastTime = std::chrono::high_resolution_clock().now();  // 更新线程执行完的时间
        idleThreadSize_++;
    }

    // threads_.erase(threadid);
    // std::cout << "threadid:" << std::this_thread::get_id() << " exit" << std::endl;
    // exitCond_.notify_all();
}

////////////////////////////////////////////////////////////
// 线程方法实现
int Thread::generateId_ = 0;

int Thread::getId() const{
    return threadId_;
}

Thread::Thread(ThreadFunc func)
    : func_(func)
    , threadId_(generateId_++)
{}
// 线程析构
Thread::~Thread(){}

void Thread::start(){
    // 创建一个线程执行一个线程函数
    std::thread t(func_, threadId_);       // C++11来说
    t.detach();                 // 设置分离线程
}

////////////////////////////////////////////////////////////
// Result方法实现
Result::Result(std::shared_ptr<Task> task, bool isValid)
    : isValid_(isValid)
    , task_(task)
{
    task_->setResult(this);
}

Any Result::get(){
    if(!isValid_){
        return "";
    }

    sem_.wait();                // task任务如果没有执行完，这里会阻塞用户的线程
    return std::move(any_);
}

void Result::setVal(Any any){       //  什么时候调用
    // 存储task的返回值
    this->any_ = std::move(any);
    sem_.post();        // 已经获取的返回值，增加信号量资源
}

////////////////////////////////////////////////////////////
// Task方法实现
void Task::exec(){
    if(result_ != nullptr){     
        result_->setVal(run()); // 这里发生多态调用
    }
}

void Task::setResult(Result* res){
    result_ = res;
}

Task::Task()
    :result_(nullptr)
{}
    