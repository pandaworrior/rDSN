#!/bin/bash
#
# Options:
#    INSTALL_DIR    <dir>
#    PORT           <port>

if [ -z "$INSTALL_DIR" ]
then
    echo "ERROR: no INSTALL_DIR specified"
    exit -1
fi

if [ -z "$PORT" ]
then
    echo "ERROR: no PORT specified"
    exit -1
fi

mkdir -p $INSTALL_DIR
if [ $? -ne 0 ]
then
    echo "ERROR: mkdir $PREFIX failed"
    exit -1
fi

cd $INSTALL_DIR

if [ ! -f zookeeper-3.4.6.tar.gz ]; then
    echo "Downloading zookeeper..."
    wget https://github.com/shengofsun/packages/raw/master/zookeeper-3.4.6.tar.gz
    if [ $? -ne 0 ]; then
        echo "ERROR: download zookeeper failed"
        exit -1
    fi
fi

if [ ! -d zookeeper-3.4.6 ]; then
    echo "Decompressing zookeeper..."
    tar xfz zookeeper-3.4.6.tar.gz
    if [ $? -ne 0 ]; then
        echo "ERROR: decompress zookeeper failed"
        exit -1
    fi
fi

ZOOKEEPER_HOME=`pwd`/zookeeper-3.4.6
ZOOKEEPER_PORT=$PORT

cp $ZOOKEEPER_HOME/conf/zoo_sample.cfg $ZOOKEEPER_HOME/conf/zoo.cfg
sed -i "s@dataDir=/tmp/zookeeper@dataDir=$ZOOKEEPER_HOME/data@" $ZOOKEEPER_HOME/conf/zoo.cfg
sed -i "s@clientPort=2181@clientPort=$ZOOKEEPER_PORT@" $ZOOKEEPER_HOME/conf/zoo.cfg

mkdir -p $ZOOKEEPER_HOME/data
$ZOOKEEPER_HOME/bin/zkServer.sh start

sleep 2
if echo ruok | nc localhost $ZOOKEEPER_PORT | grep -q imok; then
    echo "Zookeeper started at port $ZOOKEEPER_PORT"
    exit 0
else
    echo "ERROR: start zookeeper failed"
    exit -1
fi
 
