const nodemailer = require('nodemailer');
const config_module = require("./config")

/**
 * 创建发送邮件的代理
 */
let transport = nodemailer.createTransport({
    host: 'smtp.qq.com',
    port: 465,
    secure: true, // 使用SSL
    auth: {
        user: config_module.email_user, // 发送方邮箱地址
        pass: config_module.email_pass // 邮箱授权码（不是QQ密码）
    },
    tls: {
        rejectUnauthorized: true // 拒绝无效证书，防止中间人攻击
    }
});
/**
 * 发送邮件的函数
 * @param {*} mailOptions_ 发送邮件的参数
 * @returns 
 */
function SendMail(mailOptions_){
    return Promise.race([
        new Promise(function(resolve, reject){
            transport.sendMail(mailOptions_, function(error, info){
                if (error) {
                    console.log(error);
                    reject(error);
                } else {
                    console.log('邮件已成功发送：' + info.response);
                    resolve(info.response)
                }
            });
        }),
        new Promise(function(_, reject) {
            setTimeout(function() { reject(new Error('SendMail timeout (15s)')); }, 15000);
        })
    ])
}

module.exports.SendMail = SendMail