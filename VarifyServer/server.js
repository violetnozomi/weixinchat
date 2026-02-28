const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');
const path = require('path');
const { v4: uuidv4 } = require('uuid');
const emailModule = require('./email');
const const_module = require('./const');
const message_proto = require('./proto');
const config_module = require('./config');
const redis_module = require('./redis')
const PROTO_PATH = path.join(__dirname, 'message.proto');
async function GetVarifyCode(call, callback) {
    console.log("email is ", call.request.email)
    try{
        let query_res = await redis_module.GetRedis(const_module.code_prefix+call.request.email);
        console.log("query_res is ", query_res)
        if(query_res == null){

        }
        let uniqueId = query_res;
        if(query_res ==null){
            uniqueId = uuidv4();
            if (uniqueId.length > 4) {
                uniqueId = uniqueId.substring(0, 4);
            } 
            let bres = await redis_module.SetRedisExpire(const_module.code_prefix+call.request.email, uniqueId,600)
            if(!bres){
                callback(null, { email:  call.request.email,
                    error:const_module.Errors.RedisErr
                });
                return;
            }
        }

        console.log("uniqueId is ", uniqueId)
        let text_str =  '您的验证码为'+ uniqueId +'请三分钟内完成注册'
        //发送邮件
        let mailOptions = {
            from: config_module.email_user,
            to: call.request.email,
            subject: '验证码',
            text: text_str,
        };

        let send_res = await emailModule.SendMail(mailOptions);
        console.log("send res is ", send_res)

        callback(null, { email:  call.request.email,
            error:const_module.Errors.Success
        }); 


    }catch(error){
        console.log("catch error is ", error)

        callback(null, { email:  call.request.email,
            error:const_module.Errors.Exception
        }); 
    }

}
function main() {
    var server = new grpc.Server();
    
    // 添加服务
    server.addService(message_proto.VarifyService.service, { 
        GetVarifyCode: GetVarifyCode 
    });

    // 绑定端口并启动
    // 注意：这里改成 0.0.0.0 防止连接失败
    server.bindAsync('0.0.0.0:50051', grpc.ServerCredentials.createInsecure(), (err, port) => {
        if (err) {
            console.error(err);
            return;
        }
        console.log(`grpc server started at http://0.0.0.0:${port}`);
        // 注意：新版 grpc-js 在 bindAsync 回调里自动启动，不需要再写 server.start()
    });
}

main();

