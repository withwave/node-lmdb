
// This file is part of node-lmdb, the Node.js binding for lmdb
// Copyright (c) 2013 Timur Kristóf
// Licensed to you under the terms of the MIT license
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "node-lmdb.h"
#include <string.h>
#include <stdio.h>

static thread_local uint32_t currentUint32Key = 0;

void setupExportMisc(Handle<Object> exports) {
    Local<Object> versionObj = Nan::New<Object>();

    int major, minor, patch;
    char *str = mdb_version(&major, &minor, &patch);
    versionObj->Set(Nan::New<String>("versionString").ToLocalChecked(), Nan::New<String>(str).ToLocalChecked());
    versionObj->Set(Nan::New<String>("major").ToLocalChecked(), Nan::New<Integer>(major));
    versionObj->Set(Nan::New<String>("minor").ToLocalChecked(), Nan::New<Integer>(minor));
    versionObj->Set(Nan::New<String>("patch").ToLocalChecked(), Nan::New<Integer>(patch));

    exports->Set(Nan::New<String>("version").ToLocalChecked(), versionObj);
}

void setFlagFromValue(int *flags, int flag, const char *name, bool defaultValue, Local<Object> options) {
    Local<Value> opt = options->Get(Nan::New<String>(name).ToLocalChecked());
    if (opt->IsBoolean() ? opt->BooleanValue() : defaultValue) {
        *flags |= flag;
    }
}

NodeLmdbKeyType keyTypeFromOptions(const Local<Value> &val, NodeLmdbKeyType defaultKeyType) {
    if (val->IsNull() || val->IsUndefined()) {
        return defaultKeyType;
    }
    if (!val->IsObject()) {
        Nan::ThrowError("keyTypeFromOptions: Invalid argument passed to a node-lmdb function, must be an object.");
        return NodeLmdbKeyType::InvalidKey;
    }
    
    auto obj = val->ToObject();

    NodeLmdbKeyType keyType = defaultKeyType;
    int keyIsUint32 = 0;
    int keyIsBuffer = 0;
    int keyIsString = 0;
    
    setFlagFromValue(&keyIsUint32, 1, "keyIsUint32", false, obj);
    setFlagFromValue(&keyIsString, 1, "keyIsString", false, obj);
    setFlagFromValue(&keyIsBuffer, 1, "keyIsBuffer", false, obj);
    
    const char *keySpecificationErrorText = "You can't specify multiple key types at once. Either set keyIsUint32, or keyIsBuffer or keyIsString (default).";
    
    if (keyIsUint32) {
        keyType = NodeLmdbKeyType::Uint32Key;
        
        if (keyIsBuffer || keyIsString) {
            Nan::ThrowError(keySpecificationErrorText);
            return NodeLmdbKeyType::InvalidKey;
        }
    }
    else if (keyIsBuffer) {
        keyType = NodeLmdbKeyType::BinaryKey;
        
        if (keyIsUint32 || keyIsString) {
            Nan::ThrowError(keySpecificationErrorText);
            return NodeLmdbKeyType::InvalidKey;
        }
    }
    else if (keyIsString) {
        keyType = NodeLmdbKeyType::StringKey;
    }
    
    return keyType;
}

NodeLmdbKeyType inferKeyType(const Local<Value> &val) {
    if (val->IsString()) {
        return NodeLmdbKeyType::StringKey;
    }
    if (val->IsUint32()) {
        return NodeLmdbKeyType::Uint32Key;
    }
    if (node::Buffer::HasInstance(val)) {
        return NodeLmdbKeyType::BinaryKey;
    }
    
    return NodeLmdbKeyType::InvalidKey;
}

NodeLmdbKeyType inferAndValidateKeyType(const Local<Value> &key, const Local<Value> &options, NodeLmdbKeyType dbiKeyType, bool &isValid) {
    auto keyType = keyTypeFromOptions(options, NodeLmdbKeyType::DefaultKey);
    auto inferredKeyType = inferKeyType(key);
    isValid = false;
    
    if (keyType != NodeLmdbKeyType::DefaultKey && inferredKeyType != keyType) {
        Nan::ThrowError("Specified key type doesn't match the key you gave.");
        return NodeLmdbKeyType::InvalidKey;
    }
    else {
        keyType = inferredKeyType;
    }
    if (dbiKeyType == NodeLmdbKeyType::Uint32Key && keyType != NodeLmdbKeyType::Uint32Key) {
        Nan::ThrowError("You specified keyIsUint32 on the Dbi, so you can't use other key types with it.");
        return NodeLmdbKeyType::InvalidKey;
    }
    
    isValid = true;
    return keyType;
}

argtokey_callback_t argToKey(const Local<Value> &val, MDB_val &key, NodeLmdbKeyType keyType, bool &isValid) {
    isValid = false;

    if (keyType == NodeLmdbKeyType::StringKey) {
        if (!val->IsString()) {
            Nan::ThrowError("Invalid key. Should be a string. (Specified with env.openDbi)");
            return nullptr;
        }
        
        isValid = true;
        CustomExternalStringResource::writeTo(val->ToString(), &key);
        return ([](MDB_val &key) -> void {
            delete[] (uint16_t*)key.mv_data;
        });
    }
    else if (keyType == NodeLmdbKeyType::Uint32Key) {
        if (!val->IsUint32()) {
            Nan::ThrowError("Invalid key. Should be an unsigned 32-bit integer. (Specified with env.openDbi)");
            return nullptr;
        }
        
        isValid = true;
        currentUint32Key = val->Uint32Value();
        key.mv_size = sizeof(uint32_t);
        key.mv_data = &currentUint32Key;

        return nullptr;
    }
    else if (keyType == NodeLmdbKeyType::BinaryKey) {
        if (!node::Buffer::HasInstance(val)) {
            Nan::ThrowError("Invalid key. Should be a Buffer. (Specified with env.openDbi)");
            return nullptr;
        }
        
        isValid = true;
        key.mv_size = node::Buffer::Length(val);
        key.mv_data = node::Buffer::Data(val);
        
        return nullptr;
    }
    else if (keyType == NodeLmdbKeyType::InvalidKey) {
        Nan::ThrowError("Invalid key type. This might be a bug in node-lmdb.");
    }
    else {
        Nan::ThrowError("Unknown key type. This is a bug in node-lmdb.");
    }

    return nullptr;
}

Local<Value> keyToHandle(MDB_val &key, NodeLmdbKeyType keyType) {
    switch (keyType) {
    case NodeLmdbKeyType::Uint32Key:
        return Nan::New<Integer>(*((uint32_t*)key.mv_data));
    case NodeLmdbKeyType::BinaryKey:
        return valToBinary(key);
    case NodeLmdbKeyType::StringKey:
        return valToString(key);
    default:
        Nan::ThrowError("Unknown key type. This is a bug in node-lmdb.");
        return Nan::Undefined();
    }
}

Local<Value> valToStringUnsafe(MDB_val &data) {
    auto resource = new CustomExternalStringResource(&data);
    auto str = Nan::New<v8::String>(resource);

    return str.ToLocalChecked();
}

Local<Value> valToString(MDB_val &data) {
    // UTF-16 buffer
    const uint16_t *buffer = reinterpret_cast<const uint16_t*>(data.mv_data);
    // Number of UTF-16 code points
    size_t n = data.mv_size / sizeof(uint16_t);
    
    // Check zero termination
    if (n < 1 || buffer[n - 1] != 0) {
        Nan::ThrowError("Invalid zero-terminated UTF-16 string");
        return Nan::Undefined();
    }
    
    size_t length = n - 1;
    auto str = Nan::New<v8::String>(buffer, length);

    return str.ToLocalChecked();
}

Local<Value> valToBinary(MDB_val &data) {
    return Nan::CopyBuffer(
        (char*)data.mv_data,
        data.mv_size
    ).ToLocalChecked();
}

Local<Value> valToBinaryUnsafe(MDB_val &data) {
    return Nan::NewBuffer(
        (char*)data.mv_data,
        data.mv_size,
        [](char *, void *) {
            // Data belongs to LMDB, we shouldn't free it here
        },
        nullptr
    ).ToLocalChecked();
}

Local<Value> valToNumber(MDB_val &data) {
    return Nan::New<Number>(*((double*)data.mv_data));
}

Local<Value> valToBoolean(MDB_val &data) {
    return Nan::New<Boolean>(*((bool*)data.mv_data));
}

void consoleLog(const char *msg) {
    Local<String> str = Nan::New("console.log('").ToLocalChecked();
    str = String::Concat(str, Nan::New<String>(msg).ToLocalChecked());
    str = String::Concat(str, Nan::New("');").ToLocalChecked());

    Local<Script> script = Nan::CompileScript(str).ToLocalChecked();
    Nan::RunScript(script);
}

void consoleLog(Local<Value> val) {
    Local<String> str = Nan::New<String>("console.log('").ToLocalChecked();
    str = String::Concat(str, val->ToString());
    str = String::Concat(str, Nan::New<String>("');").ToLocalChecked());

    Local<Script> script = Nan::CompileScript(str).ToLocalChecked();
    Nan::RunScript(script);
}

void consoleLogN(int n) {
    char c[20];
    memset(c, 0, 20 * sizeof(char));
    sprintf(c, "%d", n);
    consoleLog(c);
}

void CustomExternalStringResource::writeTo(Handle<String> str, MDB_val *val) {
    unsigned int l = str->Length() + 1;
    uint16_t *d = new uint16_t[l];
    str->Write(d);
    d[l - 1] = 0;

    val->mv_data = d;
    val->mv_size = l * sizeof(uint16_t);
}

CustomExternalStringResource::CustomExternalStringResource(MDB_val *val) {
    // The UTF-16 data
    this->d = (uint16_t*)(val->mv_data);
    // Number of UTF-16 characters in the string
    size_t n = val->mv_size / sizeof(uint16_t);
    // Silently generate a 0 length if length invalid
    this->l = n ? (n - 1) : 0;
}

CustomExternalStringResource::~CustomExternalStringResource() { }

void CustomExternalStringResource::Dispose() {
    // No need to do anything, the data is owned by LMDB, not us
    
    // But actually need to delete the string resource itself:
    // the docs say that "The default implementation will use the delete operator."
    // while initially I thought this means using delete on the string,
    // apparently they meant just calling the destructor of this class.
    delete this;
}

const uint16_t *CustomExternalStringResource::data() const {
    return this->d;
}

size_t CustomExternalStringResource::length() const {
    return this->l;
}
