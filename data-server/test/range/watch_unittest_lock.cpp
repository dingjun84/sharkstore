﻿#include <gtest/gtest.h>
#include "helper/cpp_permission.h"

#include <fastcommon/shared_func.h>
#include "base/status.h"
#include "base/util.h"
#include "common/ds_config.h"
#include "frame/sf_util.h"
#include "proto/gen/watchpb.pb.h"
#include "proto/gen/schpb.pb.h"
#include "range/range.h"
#include "server/range_server.h"
#include "server/run_status.h"
#include "storage/store.h"

#include "helper/mock/raft_server_mock.h"
#include "helper/mock/socket_session_mock.h"
#include "range/range.h"

#include "watch/watcher.h"
#include "common/socket_base.h"
#include "test_public_funcs.h"

//extern void EncodeWatchKey(std::string *buf, const uint64_t &tableId, const std::vector<std::string *> &keys);

char level[8] = "debug";

int main(int argc, char *argv[]) {
    if(argc > 1)
        strcpy(level, argv[1]);

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

metapb::Range *genRange2();
metapb::Range *genRange1();



using namespace sharkstore::dataserver;
using namespace sharkstore::dataserver::range;
using namespace sharkstore::dataserver::storage;

class SocketBaseMock: public common::SocketBase {

public:
    virtual int Send(response_buff_t *response) {
        FLOG_DEBUG("Send mock...%s", response->buff);

        return 0;
    }
};


std::string  DecodeSingleKey(const int16_t grpFlag, const std::string &encodeBuf) {
    std::vector<std::string *> vec;
    std::string key("");
    auto buf = new std::string(encodeBuf);

    watch::Watcher watcher(1, vec);
    watcher.DecodeKey(vec, encodeBuf);

        if(grpFlag) {
            for(auto it:vec) {
                key.append(*it);
            }
        } else {
            key.assign(*vec[0]);
        }

    
   //     FLOG_DEBUG("DecodeWatchKey exception(%d), %s", int(vec.size()), EncodeToHexString(*buf).c_str());
    

    if(vec.size() > 0 && key.empty())
        key.assign(*vec[0]);

    FLOG_DEBUG("DecodeKey: %s", key.c_str());
    return key;
}

class LockTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init2();
        set_log_level(level);

        strcpy(ds_config.rocksdb_config.path, "/tmp/sharkstore_ds_store_test_");
        strcat(ds_config.rocksdb_config.path, std::to_string(getticks()).c_str());
        ds_config.range_config.recover_concurrency = 10;

        sf_socket_thread_config_t config;
        ds_config.watch_config.watcher_thread_priority = 23;
        config.send_thread_priority = 40;

        sf_socket_status_t status = {0};

        socket_.Init(&config, &status);

        range_server_ = new server::RangeServer;

        context_ = new server::ContextServer;

        context_->node_id = 1;
        context_->range_server = range_server_;
        context_->socket_session = new SocketSessionMock;
        context_->raft_server = new RaftServerMock;
        context_->run_status = new server::RunStatus;

        range_server_->Init(context_);
        now = getticks();

        {
            // begin test create range
            auto msg = new common::ProtoMessage;
            schpb::CreateRangeRequest req;
            req.set_allocated_range(genRange1());

            auto len = req.ByteSizeLong();
            msg->body.resize(len);
            ASSERT_TRUE(req.SerializeToArray(msg->body.data(), len));

            range_server_->CreateRange(msg);
            ASSERT_FALSE(range_server_->ranges_.empty());

            ASSERT_TRUE(range_server_->Find(1) != nullptr);

            std::vector<metapb::Range> metas;
            auto ret = range_server_->meta_store_->GetAllRange(&metas);

            ASSERT_TRUE(metas.size() == 1) << metas.size();
            // end test create range
        }

        {
            // begin test create range
            auto msg = new common::ProtoMessage;
            schpb::CreateRangeRequest req;
            req.set_allocated_range(genRange2());

            auto len = req.ByteSizeLong();
            msg->body.resize(len);
            ASSERT_TRUE(req.SerializeToArray(msg->body.data(), len));

            range_server_->CreateRange(msg);
            ASSERT_FALSE(range_server_->ranges_.empty());

            ASSERT_TRUE(range_server_->Find(2) != nullptr);

            std::vector<metapb::Range> metas;
            auto ret = range_server_->meta_store_->GetAllRange(&metas);

            ASSERT_TRUE(metas.size() == 2) << metas.size();
            // end test create range
        }
    }

    void TearDown() override {
        DestroyDB(ds_config.rocksdb_config.path, rocksdb::Options());

        delete context_->range_server;
        delete context_->socket_session;
        delete context_->raft_server;
        delete context_->run_status;
        delete context_;
    }

    void justGet(const int16_t &rangeId, const std::string key1, const std::string &key2, const std::string& val, const int32_t& cnt, bool prefix = false)
    {
        FLOG_DEBUG("justGet...range:%d key1:%s  key2:%s  value:%s", rangeId, key1.c_str(), key2.c_str() , val.c_str());

        auto raft = static_cast<RaftMock *>(range_server_->ranges_[rangeId]->raft_.get());
        raft->ops_.leader = 1;
        raft->SetLeaderTerm(1, 1);
        range_server_->ranges_[rangeId]->setLeaderFlag(true);

        // begin test pure_get(ok)
        auto msg = new common::ProtoMessage;
        msg->expire_time = getticks() + 1000;
        msg->begin_time = get_micro_second();
        msg->session_id = 1;
        watchpb::DsKvWatchGetMultiRequest req;

        req.set_prefix(prefix);
        req.mutable_header()->set_range_id(rangeId);
        req.mutable_header()->mutable_range_epoch()->set_conf_ver(1);
        req.mutable_header()->mutable_range_epoch()->set_version(1);

        req.mutable_kv()->set_version(0);
        req.mutable_kv()->set_tableid(1);

        req.mutable_kv()->add_key(key1);
        if(!key2.empty())
            req.mutable_kv()->add_key(key2);


        auto len = req.ByteSizeLong();
        msg->body.resize(len);
        ASSERT_TRUE(req.SerializeToArray(msg->body.data(), len));

        range_server_->PureGet(msg);

        watchpb::DsKvWatchGetMultiResponse resp;
        auto session_mock = static_cast<SocketSessionMock *>(context_->socket_session);

        //cnt为０时，仍返回编码ＯＫ　　kvs_size() == 0
        ASSERT_TRUE(session_mock->GetResult(&resp));

        FLOG_DEBUG(">>>PureGet RESP:%s", resp.DebugString().c_str());

        ASSERT_FALSE(resp.header().has_error());
        EXPECT_TRUE(resp.kvs_size() == cnt);

        if (cnt && resp.kvs_size()) {
            for(auto i = 0; i<cnt; i++)
                EXPECT_TRUE(resp.kvs(i).value() == val);
        }
    }

    void justDel(const int16_t &rangeId, const std::string &key1, const std::string &value, bool prefix = false)
    {
        FLOG_DEBUG("justDel...range:%d key1:%s  ", rangeId, key1.c_str());

        // begin test watch_delete( ok )

        // set leader
        auto raft = static_cast<RaftMock *>(range_server_->ranges_[1]->raft_.get());
        raft->ops_.leader = 1;
        raft->SetLeaderTerm(1, 1);
        range_server_->ranges_[1]->setLeaderFlag(true);

        auto msg = new common::ProtoMessage;
        msg->expire_time = getticks() + 3000;
        msg->session_id = 1;
        msg->socket = &socket_;
        msg->begin_time = get_micro_second();
        auto msgId(rand());
        FLOG_DEBUG("msg_id:%" PRId32, msgId);
        msg->msg_id = msgId;
        //msg->msg_id = 20180813;

        watchpb::DsKvWatchDeleteRequest req;

        req.mutable_header()->set_range_id(rangeId);
        req.mutable_header()->mutable_range_epoch()->set_conf_ver(1);
        req.mutable_header()->mutable_range_epoch()->set_version(1);

        req.mutable_req()->mutable_kv()->add_key(key1);

        req.mutable_req()->mutable_kv()->set_version(1);

        req.mutable_req()->set_prefix(prefix);

        auto len = req.ByteSizeLong();
        msg->body.resize(len);
        ASSERT_TRUE(req.SerializeToArray(msg->body.data(), len));

        range_server_->WatchDel(msg);

        watchpb::DsKvWatchDeleteResponse resp;
        auto session_mock = static_cast<SocketSessionMock *>(context_->socket_session);
        ASSERT_TRUE(session_mock->GetResult(&resp));

        FLOG_DEBUG("watch_del response: %s", resp.DebugString().c_str());

        ASSERT_FALSE(resp.header().has_error());

        // end test watch_delete

    }

    void LockGet(const int16_t &rangeId, const std::string key, const std::string& val, const int32_t& cnt)
    {
        FLOG_DEBUG("LockGet...range:%d key1: %s ", rangeId, key.c_str());

        auto raft = static_cast<RaftMock *>(range_server_->ranges_[rangeId]->raft_.get());
        raft->ops_.leader = 1;
        raft->SetLeaderTerm(1, 1);
        range_server_->ranges_[rangeId]->setLeaderFlag(true);

        auto msg = new common::ProtoMessage;
        msg->expire_time = getticks() + 1000;
        msg->begin_time = get_micro_second();
        msg->session_id = 1;
        kvrpcpb::DsLockGetRequest req;

        req.mutable_header()->set_range_id(rangeId);
        req.mutable_header()->mutable_range_epoch()->set_conf_ver(1);
        req.mutable_header()->mutable_range_epoch()->set_version(1);

        req.mutable_req()->set_key(key);

        auto len = req.ByteSizeLong();
        msg->body.resize(len);
        ASSERT_TRUE(req.SerializeToArray(msg->body.data(), len));

        range_server_->LockGet(msg);

        kvrpcpb::DsLockGetResponse resp;
        auto session_mock = static_cast<SocketSessionMock *>(context_->socket_session);

        //cnt为０时，仍返回编码ＯＫ　　kvs_size() == 0
        ASSERT_TRUE(session_mock->GetResult(&resp));

        FLOG_DEBUG(">>>LockGet RESP:%s", resp.DebugString().c_str());

        if(!cnt) {
            ASSERT_TRUE(resp.header().has_error());
            ASSERT_TRUE(resp.resp().code() == 1);
        } else {
            ASSERT_TRUE(resp.resp().code() == 0);
        }
    }

    void Lock(const int16_t &rangeId, const std::string &key,const std::string &value, const std::string& id, bool result = true)
    {
        FLOG_DEBUG("Lock...range:%d key1:%s  value:%s", rangeId, key.c_str(), value.c_str());

        auto raft = static_cast<RaftMock *>(range_server_->ranges_[rangeId]->raft_.get());
        raft->ops_.leader = 1 ;
        raft->SetLeaderTerm(1, 1);
        range_server_->ranges_[rangeId]->is_leader_ = true;

        // begin test watch_get (ok)
        auto msg1 = new common::ProtoMessage;
        //put first
        msg1->expire_time = getticks() + 1000;
        msg1->begin_time = get_micro_second();
        msg1->session_id = 1;
        srand(time(NULL));
        auto msgId(rand());
        FLOG_DEBUG("msg_id:%" PRId32, msgId);
        msg1->msg_id = msgId;
        msg1->socket = &socket_;
        kvrpcpb::DsLockRequest req;

        req.mutable_header()->set_range_id(rangeId);
        req.mutable_header()->mutable_range_epoch()->set_conf_ver(1);
        req.mutable_header()->mutable_range_epoch()->set_version(1);

        std::string by("localhost:80");

        req.mutable_req()->set_key(key);
        req.mutable_req()->mutable_value()->set_delete_time(1000);
        req.mutable_req()->mutable_value()->set_by(by);
        req.mutable_req()->mutable_value()->set_id(id);
        req.mutable_req()->mutable_value()->set_value(value);

        auto len1 = req.ByteSizeLong();
        msg1->body.resize(len1);
        ASSERT_TRUE(req.SerializeToArray(msg1->body.data(), len1));

        range_server_->Lock(msg1);

        kvrpcpb::DsLockResponse resp1;
        auto session_mock = static_cast<SocketSessionMock *>(context_->socket_session);
        ASSERT_TRUE(session_mock->GetResult(&resp1));
        FLOG_DEBUG("Lock first response: %s", resp1.DebugString().c_str());

        if(result)
            ASSERT_TRUE(resp1.resp().code() == 0);
        else
            ASSERT_TRUE(resp1.resp().code() == 2);

        return;
    }

    void LockWatch(const int16_t &rangeId, const std::string key, bool result = true)
    {
        FLOG_DEBUG("justWatch...range:%d key:%s ", rangeId, key.c_str());
        auto raft = static_cast<RaftMock *>(range_server_->ranges_[rangeId]->raft_.get());
        raft->ops_.leader = 1;
        raft->SetLeaderTerm(1, 1);
        range_server_->ranges_[rangeId]->is_leader_ = true;

        // begin test watch_get (key empty)
        auto msg = new common::ProtoMessage;
        msg->expire_time = getticks() + 3000;
        msg->begin_time = get_micro_second();
        msg->session_id = 1;
        msg->socket = &socket_;
        msg->msg_id = 20180813;

        watchpb::DsWatchRequest req;

        req.mutable_header()->set_range_id(rangeId);
        req.mutable_header()->mutable_range_epoch()->set_conf_ver(1);
        req.mutable_header()->mutable_range_epoch()->set_version(1);

        req.mutable_req()->mutable_kv()->add_key(key);
        //req.mutable_req()->mutable_kv()->set_version(1);
        req.mutable_req()->set_longpull(5000);
        ///////////////////////////////////////////////
        //传入大版本号，用于让watcher添加成功
        req.mutable_req()->set_startversion(0);

        auto len = req.ByteSizeLong();
        msg->body.resize(len);
        ASSERT_TRUE(req.SerializeToArray(msg->body.data(), len));

        range_server_->LockWatch(msg);

        watchpb::DsWatchResponse resp;
        auto session_mock = static_cast<SocketSessionMock *>(context_->socket_session);

        if(result)
            ASSERT_FALSE(resp.header().has_error());
        else {
            session_mock->GetResult(&resp);
            ASSERT_TRUE(resp.resp().code() == 1);
        }
    }

protected:
    server::ContextServer *context_;
    server::RangeServer *range_server_;
    int64_t now;
    SocketBaseMock socket_;
};
/*
metapb::Range *genRange1() {
    //watch::Watcher watcher;
    auto meta = new metapb::Range;
    
    std::vector<std::string*> keys;
    keys.clear();
    std::string keyStart("");
    std::string keyEnd("");
    std::string k1("01003"), k2("01004");

    keys.push_back(&k1);
    watch::Watcher watcher1(1, keys);
    watcher1.EncodeKey(&keyStart, 1, keys);

    keys.clear();
    keys.push_back(&k2);
    watch::Watcher watcher2(1, keys);
    watcher2.EncodeKey(&keyEnd, 1, keys);

    meta->set_id(1);
    //meta->set_start_key("01003");
    //meta->set_end_key("01004");
    meta->set_start_key(keyStart);
    meta->set_end_key(keyEnd);

    meta->mutable_range_epoch()->set_conf_ver(1);
    meta->mutable_range_epoch()->set_version(1);

    meta->set_table_id(1);

    auto peer = meta->add_peers();
    peer->set_id(1);
    peer->set_node_id(1);

//    peer = meta->add_peers();
//    peer->set_id(2);
//    peer->set_node_id(2);

    return meta;
}

metapb::Range *genRange2() {
    //watch::Watcher watcher;
    auto meta = new metapb::Range;

    std::vector<std::string*> keys;
    keys.clear();
    std::string keyStart("");
    std::string keyEnd("");
    std::string k1("01004"), k2("01005");

    keys.push_back(&k1);
    watch::Watcher watcher1(1, keys);
    watcher1.EncodeKey(&keyStart, 1, keys);

    keys.clear();
    keys.push_back(&k2);
    watch::Watcher watcher2(1, keys);
    watcher2.EncodeKey(&keyEnd, 1, keys);

    meta->set_id(2);
    //meta->set_start_key("01004");
    //meta->set_end_key("01005");
    meta->set_start_key(keyStart);
    meta->set_end_key(keyEnd);

    meta->mutable_range_epoch()->set_conf_ver(1);
    meta->mutable_range_epoch()->set_version(1);

    meta->set_table_id(1);

    auto peer = meta->add_peers();
    peer->set_id(1);
    peer->set_node_id(1);

    return meta;
}
*/

TEST_F(LockTest, watch_lock_get) {
    //not exist
    LockGet(1, "01003001", "", 0);

    //put a lock,then get
    Lock(1,"0100301", "val", "lock_1");
    LockGet(1, "0100301", "val", 1);

    //lock by same id, expect success
    Lock(1,"0100301", "val", "lock_1", true);

    //put a lock, already exists
    Lock(1,"0100301", "val", "lock_2", false);
    //expect add watcher success
    LockWatch(1, "0100301");
    LockWatch(1, "0100301");
    LockWatch(1, "0100301");

    //delete lock and notify watcher
    justDel(1, "0100301", "val", true);

    FLOG_INFO("sleep 10 secs, wait for watcher timeout queue pop.");
    //wait for notify
    sleep(10);

    //ok  参数１　代表期望能查询到数据
    LockGet(1, "0100301", "val", 0);

    //lock is not exist, so watch fail.
    LockWatch(1, "0100301", false);

    //Lock success
    Lock(1,"0100301", "val", "lock_3", true);

}

TEST_F(LockTest, watch_lock_expire) {
    //not exist
    LockGet(1, "01003001", "", 0);

    //put a lock,then get
    Lock(1,"0100301", "val", "lock_1");
    LockGet(1, "0100301", "val", 1);

    //lock by same id, expect success
    Lock(1,"0100301", "val", "lock_1", true);

    FLOG_INFO("sleep 5 secs, wait for lock expired.");
    sleep(5);
    //put a lock, already exists
    Lock(1,"0100301", "val", "lock_2", true);
    //LockWatch(1, "0100301");
    //justDel(1, "0100301", "val", true);

    //ok  参数１　代表期望能查询到数据
    LockGet(1, "0100301", "val", 1);
    LockWatch(1, "0100301", true);

}

