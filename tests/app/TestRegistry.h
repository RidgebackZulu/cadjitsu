#pragma once

#include <QObject>

#include <functional>
#include <vector>

// Each test file registers its QtTest class; main.cpp runs them all.
std::vector<std::function<QObject *()>> &cadjitsuTestFactories();

#define CADJITSU_REGISTER_TEST(Class)                                                                                     \
    static const bool cadjitsu_registered_##Class = (cadjitsuTestFactories().push_back([] { return new Class; }), true);
