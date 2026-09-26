#pragma once

#include <QObject>

#include <functional>
#include <vector>

// Each test file registers its QtTest class; main.cpp runs them all.
std::vector<std::function<QObject *()>> &cadlyTestFactories();

#define CADLY_REGISTER_TEST(Class)                                                                                     \
    static const bool cadly_registered_##Class = (cadlyTestFactories().push_back([] { return new Class; }), true);
