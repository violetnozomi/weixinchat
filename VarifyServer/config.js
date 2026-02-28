const fs = require('fs');
const path = require('path');

// 读取配置文件
const config_path = path.join(__dirname, 'config.ini');
const config_data = fs.readFileSync(config_path, 'utf8');
const config = JSON.parse(config_data);

const email_user = config.email.user;
const email_pass = config.email.pass;

const redis_host = config.redis.host;
const redis_port = config.redis.port;
const redis_passwd = config.redis.passwd;

module.exports = { email_user, email_pass, redis_host, redis_port, redis_passwd };
