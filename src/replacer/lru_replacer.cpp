// Copyright (c) 2023-2027 Renmin University of China
// SPDX-License-Identifier: MulanPSL-2.0

#include "lru_replacer.h"

LRUReplacer::LRUReplacer(size_t num_pages) { max_size_ = num_pages; }

LRUReplacer::~LRUReplacer() = default;

/**
 * @description: 使用LRU策略删除一个victim frame，并返回该frame的id
 * @param {frame_id_t*} frame_id 被移除的frame的id，如果没有frame被移除返回nullptr
 * @return {bool} 如果成功淘汰了一个页面则返回true，否则返回false
 */
bool LRUReplacer::victim(frame_id_t* frame_id) {
    // std::scoped_lock 同时保护容量和链表状态。
    // 它能够避免死锁发生，其构造函数能够自动进行上锁操作，析构函数会对互斥量进行解锁操作，保证线程安全。
    std::scoped_lock lock{latch_};  //  如果编译报错可以替换成其他lock

    if(LRUlist_.empty()){//如果候选的集合为空，那么没办法选帧淘汰
        return false;
    }

    auto it=LRUlist_.begin();//取出候选集合的第一个，作为移除对象
    *frame_id=*it;//给传入的参数赋值，赋值为移除的帧

    //执行删除操作
    LRUhash_.erase(*it);
    LRUlist_.erase(it);
    //*frame_id=*it;
    return true;
}

/**
 * @description: 固定指定的frame，即该页面无法被淘汰
 * @param {frame_id_t} 需要固定的frame的id
 */
void LRUReplacer::pin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};
    auto it=LRUhash_.find(frame_id);//找到候选队列里面frame_id的帧的迭代器
    if(it!=LRUhash_.end()){//判断这个frame_id的帧是不是在待淘汰集合中

        //如果找到的话，就把它移除出待淘汰集合
        LRUlist_.erase(it->second);
        LRUhash_.erase(it);
    }
}

/**
 * @description: 取消固定一个frame，代表该页面可以被淘汰
 * @param {frame_id_t} frame_id 取消固定的frame的id
 */
void LRUReplacer::unpin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};

    if(LRUhash_.find(frame_id)!=LRUhash_.end()){
        //如果已经在待淘汰集合中，那么不用管它
        return ;
    }
    //如果不在待淘汰集合中，那么就把frame_id这个帧加入待淘汰集合里面，并且在映射表里面记录
    LRUlist_.push_back(frame_id);
    auto it=std::prev(LRUlist_.end());
    LRUhash_[frame_id]=it;
}

/**
 * @description: 获取当前replacer中可以被淘汰的页面数量
 */
size_t LRUReplacer::Size() { return LRUlist_.size(); }
