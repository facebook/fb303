/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fb303/FollyLoggingHandler.h>

#include <fb303/ServiceData.h>
#include <folly/logging/LoggerDB.h>
#include <folly/test/JsonTestUtil.h>
#include <gtest/gtest.h>

using namespace facebook::fb303;
using folly::LoggerDB;

TEST(LoggingHandler, registerOptions) {
  ServiceData data;
  LoggerDB loggerDB{LoggerDB::TESTING};

  registerFollyLoggingOptionHandlers(
      "logging", "logging_full", &data, &loggerDB);
  FOLLY_EXPECT_JSON_EQ(
      R"JSON({
  "categories" : {
    "" : {
      "handlers" : [],
      "inherit" : false,
      "level" : "INFO",
      "propagate": "NONE"
    }
  },
  "handlers" : {}
})JSON",
      data.getOption("logging"));

  // Set some options with the "logging" option.
  data.setOption("logging", ".=DBG1,foo.bar=INFO");

  // Look up another log category just to define some other categories
  // with default settings.
  loggerDB.getCategory("some.other.category");

  FOLLY_EXPECT_JSON_EQ(
      R"JSON({
  "categories" : {
    "" : {
      "handlers" : [],
      "inherit" : false,
      "level" : "DBG1",
      "propagate": "NONE"
    },
    "foo.bar" : {
      "handlers" : [],
      "inherit" : true,
      "level" : "INFO",
      "propagate": "NONE"
    }
  },
  "handlers" : {}
})JSON",
      data.getOption("logging"));

  FOLLY_EXPECT_JSON_EQ(
      R"JSON({
  "categories" : {
    "" : {
      "handlers" : [],
      "inherit" : false,
      "level" : "DBG1",
      "propagate": "NONE"
    },
    "foo" : {
      "handlers" : [],
      "inherit" : true,
      "level" : "FATAL",
      "propagate": "NONE"
    },
    "foo.bar" : {
      "handlers" : [],
      "inherit" : true,
      "level" : "INFO",
      "propagate": "NONE"
    },
    "some" : {
      "handlers" : [],
      "inherit" : true,
      "level" : "FATAL",
      "propagate": "NONE"
    },
    "some.other" : {
      "handlers" : [],
      "inherit" : true,
      "level" : "FATAL",
      "propagate": "NONE"
    },
    "some.other.category" : {
      "handlers" : [],
      "inherit" : true,
      "level" : "FATAL",
      "propagate": "NONE"
    }
  },
  "handlers" : {}
})JSON",
      data.getOption("logging_full"));

  // Reset logging options with the "logging_full" option.
  data.setOption("logging_full", "test=DBG0");

  FOLLY_EXPECT_JSON_EQ(
      R"JSON({
  "categories" : {
    "" : {
      "handlers" : [],
      "inherit" : false,
      "level" : "INFO",
      "propagate": "NONE"
    },
    "test" : {
      "handlers" : [],
      "inherit" : true,
      "level" : "DBG0",
      "propagate": "NONE"
    }
  },
  "handlers" : {}
})JSON",
      data.getOption("logging"));
}
