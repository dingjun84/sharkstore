(function () {
    /**
     * lock 详情列表展示
     */
    $('#lockLists').bootstrapTable({
        url: getUrl(),
        striped: true,
        cache: false,
        pagination: true,
        pageNumber: 1,
        pageSize: 10,
        pageList: [10, 25, 50, 100],
        sidePagination: "server",
        clickToSelect: true,
        iconSize: 'outline',
        toolbar: '#lockListsToolbar',
        height: 500,
        icons: {
            refresh: 'glyphicon-repeat'
        },
        // 得到查询的参数
        queryParams: function (params) {
            //这里的键的名字和控制器的变量名必须一直，这边改动，控制器也需要改成一样的
            var temp = {
                rows: params.limit,                         //页面大小
                page: (params.offset / params.limit) + 1,   //页码
                sort: params.sort,      //排序列名
                sortOrder: params.order //排位命令（desc，asc）
            };
            return temp;
        },
        columns: [
            {field: '', checkbox: true, align: 'center'},
            {field: 'k', title: 'key', align: 'center'},
            {field: 'v', title: '配置', align: 'center'},
            {field: 'version', title: '版本', align: 'center'},
            {field: 'extend', title: '扩展', align: 'center'},
            {field: 'lock_id', title: '锁id', align: 'center'},
            {
                field: 'expired_time', title: '过期时间', align: 'center',
                formatter: function (value, row, index) {
                    if(!hasText(value) || value == 0){
                        return "";
                    }
                    return formatDate((new Date(value)), "yyyy-MM-dd hh:mm:ss");
                }
            },

            {
                field: 'upd_time', title: '更新时间', align: 'center',
                formatter: function (value, row, index) {
                    //调用下面方法进行时间戳格式化
                    if(!hasText(value) || value == 0){
                        return "";
                    }
                    return formatDate((new Date(value)), "yyyy-MM-dd hh:mm:ss");
                }
            },
            {field: 'creator', title: '客户端ip', align: 'center'}
        ],
        responseHandler: function (res) {
            if (res.code === 0) {
                return {
                    "total": res.data.total,//当前页面数据
                    "rows": res.data.data   //数据
                };
            } else {
                swal("失败", res.msg, "error");
                return {
                    "total": 0,//总页数
                    "rows": res.data   //数据
                };
            }
        }
    });
})(document, window, jQuery);

function getUrl() {
    return "/lock/lock/queryList?clusterId=" + $('#clusterId').val()
        + "&dbName=" + $('#dbName').val()
        + "&tableName=" + $('#tableName').val()
}

//能看到记录，就能修改， 支持批量
function forceUnLock() {
    var selectedApplyRows = $('#lockLists').bootstrapTable('getSelections');
    if (selectedApplyRows.length == 0) {
        swal("强制解锁", "请选择要强制解锁的记录", "error");
        return;
    }
    var clusterId = $("#clusterId").val();
    var dbName = $("#dbName").val();
    var tableName = $("#tableName").val();
    if (!hasText(clusterId) || !hasText(dbName) || !hasText(tableName)) {
        swal("强制解锁", "请选择要解锁的lock", "error");
        return;
    }
    var keys = [];
    for (var i = 0; i < selectedApplyRows.length; i++) {
        keys.push(selectedApplyRows[i].k);
    }
    if (keys.length == 0) {
        return;
    }
    swal({
            title: "强制解锁",
            type: "warning",
            showCancelButton: true,
            confirmButtonColor: "#DD6B55",
            confirmButtonText: "确认",
            closeOnConfirm: false
        },
        function () {
            $.ajax({
                url: "/lock/lock/forceUnLock",
                type: "post",
                contentType: "application/x-www-form-urlencoded; charset=UTF-8",
                dataType: "json",
                data: {
                    clusterId: clusterId,
                    dbName: dbName,
                    tableName: tableName,
                    keys:  JSON.stringify(keys)
                },
                success: function (data) {
                    if (data.code === 0) {
                        swal("强制解锁成功!", data.msg, "success");
                        window.location.href = "/page/lock/viewLock?clusterId=" + clusterId + "&dbName=" + dbName + "&tableName=" + tableName;
                    } else {
                        swal("强制解锁失败", data.msg, "error");
                    }
                },
                error: function (res) {
                    swal("强制解锁失败", res, "error");
                }
            });
        });
}

function refreshLockList() {
    var opt = {
        url: getUrl(),
    };
    $("#lockLists").bootstrapTable('refresh', opt);
}