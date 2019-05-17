# Tinyhttpd

## 工作流程

- 提取HTTP 请求报文中的请求方法(GET,POST)和url

    - 若是GET方法,且url中携带参数,则query_string指针指向参数头

    - 若是POST方法,开启CGI

- 格式化url到path数组中,表示浏览器请求的服务器文件路径

    - 若 path以 / 结尾, 或 path是个目录

        - 在path后添加index.html,表示访问主页

- 如果文件路径合法

    - 若请求方法是 GET

        - url中带参数,则调用excute_cgi函数执行cgi脚本

        - url中不带参数,直接输出服务器文件到浏览器

    - 若请求方法是 POST

        - 调用excute_cgi函数执行cgi脚本

- 读取剩余的HTTP请求报文

    - 若请求报文是 POST

        - 获取请求头不包含content_length字段,则返回400