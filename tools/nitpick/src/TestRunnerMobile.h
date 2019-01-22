//
//  TestRunnerMobile.h
//
//  Created by Nissim Hadar on 22 Jan 2019.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_testRunnerMobile_h
#define hifi_testRunnerMobile_h
#include <QObject>

class TestRunnerMobile : public QObject {
    Q_OBJECT
public:
    explicit TestRunnerMobile(QObject* parent = 0);
    ~TestRunnerMobile();
};
#endif
