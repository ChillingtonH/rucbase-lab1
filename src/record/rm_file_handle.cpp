// Copyright (c) 2023-2027 Renmin University of China
// SPDX-License-Identifier: MulanPSL-2.0

#include "rm_file_handle.h"

/**
 * @description: 获取当前表中记录号为rid的记录
 * @param {Rid&} rid 记录号，指定记录的位置
 * @param {Context*} context
 * @return {unique_ptr<RmRecord>} rid对应的记录对象指针
 */
std::unique_ptr<RmRecord> RmFileHandle::get_record(const Rid& rid, Context* context) const {
    //取页会pin，后面要unpin
    RmPageHandle ph=fetch_page_handle(rid.page_no);

    //假如这个槽没有记录的话
    if(!Bitmap::is_set(ph.bitmap,rid.slot_no)){
        //先还页再抛异常，否则这一帧会被永久pin住
        buffer_pool_manager_->unpin_page(ph.page->get_page_id(),false);
        throw RecordNotFoundError(rid.page_no,rid.slot_no);
    }

    //复制一份记录，避免页之后被淘汰导致数据失效
    auto record=std::make_unique<RmRecord>(file_hdr_.record_size,ph.get_slot(rid.slot_no));
    buffer_pool_manager_->unpin_page(ph.page->get_page_id(),false);
    return record;
}

/**
 * @description: 在当前表中插入一条记录，不指定插入位置
 * @param {const char*} buf 要插入的记录数据，只读取 record_size 字节
 * @param {Context*} context
 * @return {Rid} 插入的记录的记录号（位置）
 */
Rid RmFileHandle::insert_record(const char* buf, Context* context) {
    //取一页还有空位的页，没有的话就新建。返回的时候页处于pin状态
    RmPageHandle ph=create_page_handle();
    //找到第一个值为0的位，即第一个空槽
    int slot_no=Bitmap::first_bit(false,ph.bitmap,file_hdr_.num_records_per_page);
    Bitmap::set(ph.bitmap,slot_no);//标记这个槽被占用了

    //拷贝record_size的字节，buf可能比一条记录长
    memcpy(ph.get_slot(slot_no),buf,file_hdr_.record_size);
    ph.page_hdr->num_records++;

    //页已满则从空闲页链表摘除，避免后续插入再取到它
    if(ph.page_hdr->num_records==file_hdr_.num_records_per_page){
        file_hdr_.first_free_page_no=ph.page_hdr->next_free_page_no;
    }

    //内容已修改，标脏后归还
    buffer_pool_manager_->unpin_page(ph.page->get_page_id(),true);
    return Rid{.page_no = ph.page->get_page_id().page_no, .slot_no = slot_no};
}

/**
 * @description: 在当前表中的指定位置插入一条记录
 * @param {Rid&} rid 要插入记录的位置
 * @param {const char*} buf 要插入的记录数据，只读取 record_size 字节
 */
void RmFileHandle::insert_record(const Rid& rid, const char* buf) {
    // 后续恢复/回滚实验使用的扩展接口，不属于 Lab1 评分任务。
}

/**
 * @description: 删除记录文件中记录号为rid的记录
 * @param {Rid&} rid 要删除的记录的记录号（位置）
 * @param {Context*} context
 */
void RmFileHandle::delete_record(const Rid& rid, Context* context) {
    RmPageHandle ph=fetch_page_handle(rid.page_no);
    if(!Bitmap::is_set(ph.bitmap,rid.slot_no)){
        buffer_pool_manager_->unpin_page(ph.page->get_page_id(),false);
        throw RecordNotFoundError(rid.page_no,rid.slot_no);
    }

    //必须在修改num_records之前判断，否则条件永远不成立
    bool was_full=(ph.page_hdr->num_records==file_hdr_.num_records_per_page);

    Bitmap::reset(ph.bitmap,rid.slot_no);
    ph.page_hdr->num_records--;

    //只有原本满的页才需要挂回链表；原本不满的页已经在链表中
    if(was_full){
        release_page_handle(ph);
    }

    buffer_pool_manager_->unpin_page(ph.page->get_page_id(),true);
}

/**
 * @description: 更新记录文件中记录号为rid的记录
 * @param {Rid&} rid 要更新的记录的记录号（位置）
 * @param {const char*} buf 新记录的数据，只读取 record_size 字节
 * @param {Context*} context
 */
void RmFileHandle::update_record(const Rid& rid, const char* buf, Context* context) {
    RmPageHandle ph=fetch_page_handle(rid.page_no);
    if(!Bitmap::is_set(ph.bitmap,rid.slot_no)){
        buffer_pool_manager_->unpin_page(ph.page->get_page_id(),false);
        throw RecordNotFoundError(rid.page_no,rid.slot_no);
    }

    //记录长度固定，原地覆盖即可，不需要移动其他记录
    memcpy(ph.get_slot(rid.slot_no),buf,file_hdr_.record_size);

    //内容已修改，标脏
    buffer_pool_manager_->unpin_page(ph.page->get_page_id(),true);
}

/**
 * 以下函数为辅助函数，仅提供参考，可以选择完成如下函数，也可以删除如下函数，在单元测试中不涉及如下函数接口的直接调用
 */
/**
 * @description: 获取指定页面的页面句柄
 * @param {int} page_no 页面号
 * @return {RmPageHandle} 指定页面的句柄
 */
RmPageHandle RmFileHandle::fetch_page_handle(int page_no) const {
    //页号必须落在[0, num_pages)内，否则会读到文件末尾之外
    if(page_no<0||page_no>=file_hdr_.num_pages){
        throw PageNotExistError(
            disk_manager_->get_file_name(fd_),
            page_no
        );
    }

    Page* page=buffer_pool_manager_->fetch_page(PageId{fd_,page_no});

    //所有帧都被pin住时，BPM返回nullptr
    if(page==nullptr){
        throw PageNotExistError(
            disk_manager_->get_file_name(fd_),
            page_no
        );
    }

    //返回的页保持pin，由调用方负责unpin
    return {&file_hdr_,page};
}

/**
 * @description: 创建一个新的page handle
 * @return {RmPageHandle} 新的PageHandle
 */
RmPageHandle RmFileHandle::create_new_page_handle() {
    //页号由BPM分配，这里只提供fd
    PageId page_id={fd_,INVALID_PAGE_ID};
    Page* page=buffer_pool_manager_->new_page(&page_id);
    if(page==nullptr){
        throw PageNotExistError(
            disk_manager_->get_file_name(fd_),
            page_id.page_no
        );
    }
    RmPageHandle res={&file_hdr_,page};

    //新页内容全为0，next_free_page_no必须显式接上原链表头，
    //否则0会被误认为合法的下一页号
    res.page_hdr->next_free_page_no=file_hdr_.first_free_page_no;
    res.page_hdr->num_records=0;
    Bitmap::init(res.bitmap,file_hdr_.bitmap_size);

    file_hdr_.num_pages++;

    //新页一定有空位，插到空闲页链表头部
    file_hdr_.first_free_page_no=page_id.page_no;

    return res;
}

/**
 * @brief 创建或获取一个空闲的page handle
 *
 * @return RmPageHandle 返回生成的空闲page handle
 * @note pin the page, remember to unpin it outside!
 */
RmPageHandle RmFileHandle::create_page_handle() {
    //first_free_page_no为-1表示没有还有空位的页
    if(file_hdr_.first_free_page_no==RM_NO_PAGE){
        return create_new_page_handle();
    }else{
        //链表头的页一定还有空槽
        return fetch_page_handle(file_hdr_.first_free_page_no);
    }
}

/**
 * @description: 当一个页面从没有空闲空间的状态变为有空闲空间状态时，更新文件头和页头中空闲页面相关的元数据
 */
void RmFileHandle::release_page_handle(RmPageHandle& page_handle) {
    //头插：这一页指向原来的链表头，再让文件头指向它
    page_handle.page_hdr->next_free_page_no=file_hdr_.first_free_page_no;
    file_hdr_.first_free_page_no=page_handle.page->get_page_id().page_no;
}
