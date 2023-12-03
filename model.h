#ifndef __MODEL_H__
#define __MODEL_H__

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ggml/ggml.h"
#include "json.hpp"
#include "zip.h"

enum SDVersion {
    VERSION_1_x,
    VERSION_2_x,
    VERSION_XL,
    VERSION_COUNT,
};

struct TensorStorage {
    std::string name;
    ggml_type type = GGML_TYPE_F32;
    bool is_bf16   = false;
    int64_t ne[4]  = {1, 1, 1, 1};
    int n_dims     = 0;

    size_t file_index = 0;
    int index_in_zip  = -1;  // >= means stored in a zip file
    size_t offset     = 0;   // offset in file

    TensorStorage() = default;

    TensorStorage(const std::string& name, ggml_type type, int64_t* ne, int n_dims, size_t file_index, size_t offset = 0)
        : name(name), type(type), n_dims(n_dims), file_index(file_index), offset(offset) {
        for (int i = 0; i < n_dims; i++) {
            this->ne[i] = ne[i];
        }
    }

    int64_t nelements() const {
        return ne[0] * ne[1] * ne[2] * ne[3];
    }

    int64_t nbytes() const {
        return nelements() * ggml_type_size(type) / ggml_blck_size(type);
    }

    int64_t nbytes_to_read() const {
        if (is_bf16) {
            return nbytes() / 2;
        } else {
            return nbytes();
        }
    }

    void unsqueeze() {
        if (n_dims == 2) {
            n_dims = 4;
            ne[3]  = ne[1];
            ne[2]  = ne[0];
            ne[1]  = 1;
            ne[0]  = 1;
        }
    }

    std::vector<TensorStorage> chunk(size_t n) {
        std::vector<TensorStorage> chunks;
        size_t chunk_size = nbytes_to_read() / n;
        reverse_ne();
        for (int i = 0; i < n; i++) {
            TensorStorage chunk_i = *this;
            chunk_i.ne[0]         = ne[0] / n;
            chunk_i.offset        = offset + i * chunk_size;
            chunk_i.reverse_ne();
            chunks.push_back(chunk_i);
        }
        reverse_ne();
        return chunks;
    }

    void reverse_ne() {
        int64_t new_ne[4] = {1, 1, 1, 1};
        for (int i = 0; i < n_dims; i++) {
            new_ne[i] = ne[n_dims - 1 - i];
        }
        for (int i = 0; i < n_dims; i++) {
            ne[i] = new_ne[i];
        }
    }
};

typedef std::function<bool(const TensorStorage&, ggml_tensor**)> on_new_tensor_cb_t;
typedef std::function<void(const std::string&, int32_t)> on_new_token_cb_t;

class ModelLoader {
protected:
    std::vector<std::string> file_paths_;
    std::vector<TensorStorage> tensor_storages;

public:
    virtual bool init_from_file(const std::string& file_path, const std::string& prefix = "");
    virtual bool init_from_files(const std::vector<std::string>& file_paths);
    virtual SDVersion get_sd_version();
    virtual ggml_type get_sd_wtype();
    virtual bool load_vocab(on_new_token_cb_t on_new_token_cb);
    virtual bool load_tensors(on_new_tensor_cb_t on_new_tensor_cb);
    virtual int64_t cal_mem_size();
    virtual ~ModelLoader() = default;
};

class GGUFModelLoader : public ModelLoader {
public:
    bool init_from_file(const std::string& file_path, const std::string& prefix = "");
};

class SafeTensorsModelLoader : public ModelLoader {
public:
    bool init_from_file(const std::string& file_path, const std::string& prefix = "");
};

class CkptModelLoader : public ModelLoader {
private:
    bool parse_data_pkl(uint8_t* buffer,
                        size_t buffer_size,
                        zip_t* zip,
                        std::string dir,
                        size_t file_index,
                        const std::string& prefix);

public:
    bool init_from_file(const std::string& file_path, const std::string& prefix = "");
};

class DiffusersModelLoader : public SafeTensorsModelLoader {
public:
    bool init_from_file(const std::string& file_path, const std::string& prefix = "");
};

ModelLoader* init_model_loader_from_file(const std::string& file_path);

#endif  // __MODEL_H__