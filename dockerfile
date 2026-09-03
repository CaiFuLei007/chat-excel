FROM ubuntu:24.04

RUN mkdir -p /home/chat_excel/
WORKDIR /home/chat_excel/

# 3. 安装 curl 与 envsubst(gettext-base) 工具
RUN apt-get update && apt-get install -y curl gettext-base && apt-get clean && rm -rf /var/lib/apt/lists/*

# 4. 拷贝必要的文件到工作目录(可执行文件与运行时库均由 CMake 聚合到 build/bin, 从这里取)
COPY ./build/bin/*_service ./
COPY ./build/bin/lib/*.so* /usr/lib/
COPY ./build/bin/prompt_template/ ./prompt_template/
COPY ./conf_templates/ ./conf_templates/
COPY ./entrypoint.sh ./
COPY ./www/ ./www/

# 5. 刷新动态链接器缓存, 使打入的运行时依赖库可被找到
RUN ldconfig

# 6. 为可执行程序和启动脚本设置可执行权限
RUN chmod +x ./*

# 7. 设置入口启动命令
ENTRYPOINT ["./entrypoint.sh"]
