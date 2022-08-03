
配置jsoncpp
===============
在jsoncpp文件夹中下载jsoncpp源码：
wget https://github.com/open-source-parsers/jsoncpp/archive/master.zip

解压缩源码文件：
unzip -x master.zip

cmake源码安装jsoncpp：
cd jsoncpp-master
mkdir -p ./build/debug
cd ./build/debug
cmake -DCMAKE_BUILD_TYPE=debug -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_INCLUDEDIR=include/jsoncpp -DARCHIVE_INSTALL_DIR=. -G "Unix Makefiles" ../..
sudo make
sudo make install

切换到项目根目录

通过链接建立关系
sudo apt-get install libjsoncpp-dev 
sudo ln -s /usr/include/jsoncpp/json/ /usr/include/json

编译运行
sh ./build.sh
./server

参考
cmake编译jsoncpp   https://blog.csdn.net/qq_40199447/article/details/105379634
报错解决 https://blog.csdn.net/qq_41821678/article/details/120331269




