// forja-core: JSON mínimo, sem exceções, com serialização canônica.
// Canônico = chaves ordenadas por bytes, sem espaços, inteiros sem expoente,
// strings em UTF-8 cru com escapes mínimos. É a base do digest das receitas.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace forja {

class Json {
public:
    enum class Type : uint8_t { Null, Bool, Int, Num, Str, Arr, Obj };

    Json() = default;
    Json(bool v) : t_(Type::Bool), b_(v) {}
    Json(int v) : t_(Type::Int), i_(v) {}
    Json(int64_t v) : t_(Type::Int), i_(v) {}
    Json(uint32_t v) : t_(Type::Int), i_(static_cast<int64_t>(v)) {}
    Json(double v) : t_(Type::Num), d_(v) {}
    Json(const char* v) : t_(Type::Str), s_(v) {}
    Json(std::string v) : t_(Type::Str), s_(std::move(v)) {}

    static Json arr() { Json j; j.t_ = Type::Arr; return j; }
    static Json obj() { Json j; j.t_ = Type::Obj; return j; }

    Type type() const { return t_; }
    bool is_null() const { return t_ == Type::Null; }
    bool is_bool() const { return t_ == Type::Bool; }
    bool is_int() const { return t_ == Type::Int; }
    bool is_num() const { return t_ == Type::Int || t_ == Type::Num; }
    bool is_str() const { return t_ == Type::Str; }
    bool is_arr() const { return t_ == Type::Arr; }
    bool is_obj() const { return t_ == Type::Obj; }

    bool as_bool(bool def = false) const { return t_ == Type::Bool ? b_ : def; }
    int64_t as_int(int64_t def = 0) const {
        if (t_ == Type::Int) return i_;
        if (t_ == Type::Num) return static_cast<int64_t>(d_);
        return def;
    }
    double as_num(double def = 0) const {
        if (t_ == Type::Num) return d_;
        if (t_ == Type::Int) return static_cast<double>(i_);
        return def;
    }
    const std::string& as_str() const { static const std::string e; return t_ == Type::Str ? s_ : e; }
    std::string str_or(const std::string& def) const { return t_ == Type::Str ? s_ : def; }

    // Array
    size_t size() const { return t_ == Type::Arr ? a_.size() : (t_ == Type::Obj ? o_.size() : 0); }
    const Json& at(size_t i) const { static const Json n; return (t_ == Type::Arr && i < a_.size()) ? a_[i] : n; }
    Json& at_mut(size_t i) { return a_[i]; }
    Json& push(Json v) { if (t_ != Type::Arr) { *this = arr(); } a_.push_back(std::move(v)); return a_.back(); }
    std::vector<Json>& items() { return a_; }
    const std::vector<Json>& items() const { return a_; }
    void erase_at(size_t i) { if (t_ == Type::Arr && i < a_.size()) a_.erase(a_.begin() + static_cast<long>(i)); }

    // Object (ordem de inserção preservada; canônico ordena)
    bool has(const std::string& k) const;
    const Json& get(const std::string& k) const;
    Json* find(const std::string& k);
    Json& set(const std::string& k, Json v);
    Json& operator[](const std::string& k);  // cria se não existir
    bool erase(const std::string& k);
    const std::vector<std::pair<std::string, Json>>& members() const { return o_; }
    std::vector<std::pair<std::string, Json>>& members_mut() { return o_; }

    std::string dump(bool canonical = false) const;
    std::string pretty(int indent = 2) const;
    bool operator==(const Json& o) const;
    bool operator!=(const Json& o) const { return !(*this == o); }

    // Retorna false e preenche err em caso de erro. Profundidade máxima 64.
    static bool parse(const std::string& text, Json& out, std::string* err = nullptr);

private:
    void dump_to(std::string& out, bool canonical) const;
    void pretty_to(std::string& out, int indent, int depth) const;
    Type t_ = Type::Null;
    bool b_ = false;
    int64_t i_ = 0;
    double d_ = 0;
    std::string s_;
    std::vector<Json> a_;
    std::vector<std::pair<std::string, Json>> o_;
};

// Atalhos de leitura usados por todo o núcleo
inline int64_t jint(const Json& o, const char* k, int64_t def = 0) { return o.get(k).as_int(def); }
inline std::string jstr(const Json& o, const char* k, const std::string& def = "") { return o.get(k).str_or(def); }

std::string json_escape(const std::string& s);

}  // namespace forja
