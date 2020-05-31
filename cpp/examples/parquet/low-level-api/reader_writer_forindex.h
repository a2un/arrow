// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements. See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership. The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the License for the
// specific language governing permissions and limitations
// under the License.

#include <arrow/io/file.h>
#include <arrow/util/logging.h>

#include <parquet/api/reader.h>
#include <parquet/api/writer.h>

using parquet::ConvertedType;
using parquet::Repetition;
using parquet::Type;
using parquet::schema::GroupNode;
using parquet::schema::PrimitiveNode;

constexpr int FIXED_LENGTH = 10;

static std::shared_ptr<GroupNode> SetupSchema(int type_num) {
  parquet::schema::NodeVector fields;

  switch(type_num) {
      case 0:{
        // Create a primitive node named 'int32_field' with type:INT32, repetition:REQUIRED,
       // logical type:TIME_MILLIS
         fields.push_back(PrimitiveNode::Make("int32_field1", Repetition::REQUIRED, Type::INT32,
                                       ConvertedType::NONE));
        break;
      }
      case 1:{
        // Create a primitive node named 'int64_field' with type:INT64, repetition:REPEATED
        fields.push_back(PrimitiveNode::Make("int64_field1", Repetition::REQUIRED, Type::INT64,
                                       ConvertedType::NONE));
        break;
      }
      case 2:{
        fields.push_back(PrimitiveNode::Make("float_field1", Repetition::REQUIRED, Type::FLOAT,
                                       ConvertedType::NONE));
        break;
      }
      case 3:{
        fields.push_back(PrimitiveNode::Make("double_field1", Repetition::REQUIRED, Type::DOUBLE,
                                       ConvertedType::NONE));
        break;
      }
      case 4:{
        // Create a primitive node named 'ba_field' with type:BYTE_ARRAY, repetition:OPTIONAL
       fields.push_back(PrimitiveNode::Make("ba_field1", Repetition::OPTIONAL, Type::BYTE_ARRAY,
                                       ConvertedType::NONE));
        break;
      }
      default:{
        break;
      }
  }

  // Create a GroupNode named 'schema' using the primitive nodes defined above
  // This GroupNode is the root node of the schema tree
  return std::static_pointer_cast<GroupNode>(
      GroupNode::Make("schema", Repetition::REQUIRED, fields));
}
