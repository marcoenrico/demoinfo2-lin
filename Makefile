EXECUTABLE=demoinfo2

CPP_FILES=$(wildcard *.cpp)
OBJ_FILES=$(CPP_FILES:.cpp=.o)
PROTO_SRC_FILES=$(wildcard *.proto)
PROTO_CPP_FILES=$(addprefix generated_proto/,$(PROTO_SRC_FILES:.proto=.pb.cc))
PROTO_OBJ_FILES=$(PROTO_CPP_FILES:.cc=.o)
PROTO_PY_FILES=$(addprefix generated_python/,$(PROTO_SRC_FILES:.proto=.pb.py))
PROTO_JAVA_FILES=$(addprefix generated_java/,$(PROTO_SRC_FILES:.proto=.pb.java))

LD_FLAGS=
LIBRARIES = -lsnappy -lprotobuf -lpthread
CC_FLAGS=-I/usr/include
PROTOBUF_FLAGS=-I/usr/include

all: ${EXECUTABLE} generated

generated: ${PROTO_CPP_FILES} ${PROTO_PY_FILES} ${PROTO_JAVA_FILES}

clean:
	rm -f ${EXECUTABLE}
	rm -f *.o
	rm -f generated_proto/*
	rm -f generated_python/*
	rm -f generated_java/*

generated_proto/%.pb.cc: %.proto
	protoc ${PROTO_SRC_FILES} ${PROTOBUF_FLAGS} -I. --cpp_out=generated_proto

generated_python/%.pb.py: %.proto
	protoc ${PROTO_SRC_FILES} ${PROTOBUF_FLAGS} -I. --python_out=generated_python

generated_java/%.pb.java: %.proto
	protoc ${PROTO_SRC_FILES} ${PROTOBUF_FLAGS} -I. --java_out=generated_java

${EXECUTABLE}: ${PROTO_OBJ_FILES} ${OBJ_FILES}
	g++ ${LD_FLAGS} -o $@ ${OBJ_FILES} ${PROTO_OBJ_FILES} ${LIBRARIES}

.cpp.o: ${CPP_FILES}
	g++ ${CC_FLAGS} -c -o $@ $<

.cc.o: ${PROTO_CPP_FILES}
	g++ ${CC_FLAGS} -c -o $@ $<

.PHONY: all clean generated
