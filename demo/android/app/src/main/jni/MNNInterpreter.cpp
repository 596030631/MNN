//
// Created by sjh on 2022/1/10.
//
#include "MNNInterpreter.h"

MNNInterpreter::MNNInterpreter() {
    interpreter = nullptr;
    session = nullptr;
    inputTensor = nullptr;
    outputTensor = nullptr;
    sessionRunning = false;
}

MNNInterpreter::~MNNInterpreter() {
    if (interpreter) delete (interpreter);
    interpreter = nullptr;
    // TODO release
    session = nullptr;
};

MNN::Interpreter *MNNInterpreter::loadModelFromFile(const char *modelName) {
    if (!interpreter) interpreter = MNN::Interpreter::createFromFile(modelName);
    LOGW("MNNInterpreter loadModelFromFile:%s", modelName);
    return interpreter;
}

MNN::Session *MNNInterpreter::createSession(const MNN::ScheduleConfig &config) {
    if (!session) session = interpreter->createSession(config);
    LOGW("MNNInterpreter createSession");
    return session;
}

MNN::Tensor *MNNInterpreter::getSessionInput(const char *name) {
    if (!inputTensor && interpreter && session) {
        LOGD("初始化输入Tensor");
        inputTensor = interpreter->getSessionInput(session, name);
        auto dims = inputTensor->buffer().dimensions;
        std::vector<int> v(dims);
        for (int i = 0; i < dims; ++i) {
            v[i] = inputTensor->length(i);
            LOGD("dims[%d] len[%d]", i, v[i]);
        }
        v[0] = 1;
        resizeSession(inputTensor, v);
    }
    LOGW("MNNInterpreter getSessionInput");
    return inputTensor;
}

MNN::Tensor *MNNInterpreter::getSessionOutput(const char *name) {
    if (interpreter && session) {
        outputTensor = interpreter->getSessionOutput(session, nullptr);
    }
    LOGW("MNNInterpreter getSessionOutput");
    return nullptr;
}

void MNNInterpreter::testRunSession() {
    if (!sessionRunning && interpreter && session) {
        sessionRunning = true;
        LOGW("MNNInterpreter testRunSession start");
        auto error = interpreter->runSession(session);
        LOGW("MNNInterpreter testRunSession end");
        LOGE("RunSession Error:%d:", error);
        if (error == 0) {
            outputTensor = getSessionOutput(nullptr);
            auto nchwTensor = new MNN::Tensor(outputTensor, MNN::Tensor::CAFFE);
            outputTensor->copyToHostTensor(nchwTensor);
            auto score = nchwTensor->host<float>()[0];
            auto index = nchwTensor->host<float>()[1];
            LOGD("Score[%f] Index[%f]", score, index);
            delete nchwTensor;
        }
        sessionRunning = false;
    }
}

void MNNInterpreter::resizeSession(MNN::Tensor *pTensor, const std::vector<int> &vector) {
    if (interpreter) {
        LOGW("Resize Tensor");
        interpreter->resizeTensor(pTensor, vector);
    }
}

bool MNNInterpreter::isRunSession() const {
    return sessionRunning;
}

