//
// Created by sjh on 2022/1/10.
//

#ifndef ANDROID_MNNINTERPRETER_H
#define ANDROID_MNNINTERPRETER_H

#include <MNN/Interpreter.hpp>
#include <MNN/ImageProcess.hpp>
#include <MNN/Tensor.hpp>
#include <jni.h>
#include "ALog.h"

class MNNInterpreter {
private:
    MNN::Interpreter *interpreter;
    MNN::Session *session;
    MNN::Tensor *inputTensor;
    bool sessionRunning;

    MNNInterpreter();

    ~MNNInterpreter();

public:
    static MNNInterpreter &getInstance() {
        static MNNInterpreter instance;
        return instance;
    }

    MNN::Interpreter *loadModelFromFile(const char *modelName);

    MNN::Session *createSession(const MNN::ScheduleConfig &config);

    MNN::Tensor *getSessionInput(const char *name);

    MNN::Tensor *getSessionOutput(const char *name);

    void testRunSession();

    void resizeSession(MNN::Tensor *pTensor, const std::vector<int>& vector);

    bool isRunSession() const;
};

#endif //ANDROID_MNNINTERPRETER_H
