// Copyright (c) 2023-2027 Renmin University of China
// SPDX-License-Identifier: MulanPSL-2.0

#include "rm_scan.h"

#include "rm_file_handle.h"

/**
 * @brief 初始化file_handle和rid
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle* file_handle) : file_handle_(file_handle) {
    // 从第一张数据页开始扫描（第 0 页是文件头）
    rid_.page_no=RM_FIRST_RECORD_PAGE;

    // -1 表示“第 0 槽之前”，next() 会从第 0 位开始找
    rid_.slot_no=-1;

    next();
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
    // 逐页查找，直到越过最后一页
    while(rid_.page_no<file_handle_->file_hdr_.num_pages){
        RmPageHandle ph=file_handle_->fetch_page_handle(rid_.page_no);

        // 从当前槽之后找下一个被占用的槽；找不到时返回 num_records_per_page
        rid_.slot_no=Bitmap::next_bit(
            true,
            ph.bitmap,
            file_handle_->file_hdr_.num_records_per_page,
            rid_.slot_no
        );

        // 扫描只读，但也必须还页
        file_handle_->buffer_pool_manager_->unpin_page(ph.page->get_page_id(),false);

        if(rid_.slot_no<file_handle_->file_hdr_.num_records_per_page){
            return ;    // 找到一条记录
        }
        // 本页没有更多记录，换下一页，槽位重置
        rid_.page_no++;
        rid_.slot_no=-1;
    }
}

/**
 * @brief ​ 判断是否到达文件末尾
 */
bool RmScan::is_end() const {
    // page_no 到达总页数，说明已经越过最后一页
    return rid_.page_no>=file_handle_->file_hdr_.num_pages;
}

/**
 * @brief RmScan内部存放的rid
 */
Rid RmScan::rid() const { return rid_; }
