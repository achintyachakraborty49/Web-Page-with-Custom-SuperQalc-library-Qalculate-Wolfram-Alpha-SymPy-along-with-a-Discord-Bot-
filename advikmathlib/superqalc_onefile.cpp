#include <iomanip>
// superqalc_onefile.cpp
// Single-file super calculator with units, GMP/MPFR big-numbers, overflow safe exponentiation,
// 'to' operator for unit conversion, smart unit printing, and CLI flags.
// Build: g++ -O2 superqalc_onefile.cpp -o superqalc -lgmp -lmpfr -std=c++17
//
// Author: assistant demo
// Date: 2025-08-10

#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <vector>
#include <cmath>
#include <gmp.h>
#include <mpfr.h>
#include <unistd.h>
#include <sys/select.h>

using namespace std;

// ----------------- Configuration -----------------
static const long double DEFAULT_MAX_DIGITS = 1e6L; // if estimated digits > this -> approximate
static const int DEFAULT_MPFR_PREC = 256; // bits for mpfr
static const int CLI_ABORT_POLL_MS = 120; // ms polling while "Processing"

// ----------------- Utilities -----------------
static string trim(const string &s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static bool enter_pressed_nonblocking() {
    fd_set set; FD_ZERO(&set); FD_SET(0, &set);
    struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 0;
    int rv = select(1, &set, NULL, NULL, &tv);
    if (rv > 0) {
        char buf[2] = {0}; ssize_t n = read(0, buf, 1);
        if (n > 0 && (buf[0] == '\n' || buf[0] == '\r')) return true;
    }
    return false;
}

// safe string to long double
static long double safe_stold(const string &s) {
    try { return stold(s); } catch(...) { return 0.0L; }
}

// ----------------- Dimension & Unit System -----------------
struct Dimension {
    // canonical order: L M T I Theta N J  (7 SI base dims: length, mass, time, current, temperature, amount, luminous intensity)
    int p[7];
    Dimension() { memset(p, 0, sizeof(p)); }
    bool operator==(const Dimension &o) const {
        for (int i = 0; i < 7; ++i) if (p[i] != o.p[i]) return false;
        return true;
    }
    Dimension operator+(const Dimension &o) const { Dimension r; for (int i = 0; i < 7; ++i) r.p[i] = p[i] + o.p[i]; return r; }
    Dimension operator-(const Dimension &o) const { Dimension r; for (int i = 0; i < 7; ++i) r.p[i] = p[i] - o.p[i]; return r; }
    Dimension pow_int(int n) const { Dimension r; for (int i = 0; i < 7; ++i) r.p[i] = p[i] * n; return r; }
    string to_string_compound() const {
        // produce string like m^1 kg^0 s^-2 (filter zeros)
        // We'll map indices to base names externally.
        return ""; // filled by pretty printer which has access to names
    }
};

struct Unit {
    string name;
    mpfr_t factor; // multiplicative factor to convert a numeric value in this unit to SI base numeric (value * factor -> SI numeric)
    Dimension dim;
    Unit(const string &n) : name(n) { mpfr_init2(factor, DEFAULT_MPFR_PREC); mpfr_set_d(factor, 1.0, MPFR_RNDN); }
    ~Unit() { mpfr_clear(factor); }
};

using UnitPtr = shared_ptr<Unit>;

struct UnitRegistry {
    unordered_map<string, UnitPtr> table;
    vector<pair<string, Dimension>> baseNames; // mapping index -> name for pretty printing (L M T I Theta N J)
    UnitRegistry() { init_units(); }

    void add_unit(const string &name, long double factor, const Dimension &dim) {
        UnitPtr u = make_shared<Unit>(name);
        mpfr_set_d(u->factor, factor, MPFR_RNDN);
        u->dim = dim;
        table[name] = u;
    }
    UnitPtr lookup(const string &name) const {
        auto it = table.find(name);
        if (it == table.end()) return nullptr;
        return it->second;
    }
    vector<UnitPtr> units_with_dim(const Dimension &d) const {
        vector<UnitPtr> out;
        for (auto &kv : table) if (kv.second->dim == d) out.push_back(kv.second);
        return out;
    }

    void init_units() {
        // Setup baseNames for pretty printing
        // indices 0..6 -> L M T I Theta N J
        baseNames = { {"m", Dimension()}, {"kg", Dimension()}, {"s", Dimension()}, {"A", Dimension()}, {"K", Dimension()}, {"mol", Dimension()}, {"cd", Dimension()} };
        baseNames[0].second.p[0] = 1;
        baseNames[1].second.p[1] = 1;
        baseNames[2].second.p[2] = 1;
        baseNames[3].second.p[3] = 1;
        baseNames[4].second.p[4] = 1;
        baseNames[5].second.p[5] = 1;
        baseNames[6].second.p[6] = 1;

        // Base SI
        Dimension dL; dL.p[0] = 1; add_unit("m", 1.0L, dL);
        Dimension dM; dM.p[1] = 1; add_unit("kg", 1.0L, dM);
        Dimension dT; dT.p[2] = 1; add_unit("s", 1.0L, dT);
        Dimension dI; dI.p[3] = 1; add_unit("A", 1.0L, dI);
        Dimension dTh; dTh.p[4] = 1; add_unit("K", 1.0L, dTh);
        Dimension dN; dN.p[5] = 1; add_unit("mol", 1.0L, dN);
        Dimension dJ; dJ.p[6] = 1; add_unit("cd", 1.0L, dJ);

        // dimensionless
        Dimension d0; add_unit("", 1.0L, d0);

        // common prefixes for units (we add explicit prefixed names for convenience)
        add_unit("cm", 0.01L, dL);
        add_unit("mm", 0.001L, dL);
        add_unit("km", 1000.0L, dL);
        add_unit("um", 1e-6L, dL); // micrometer
        add_unit("nm", 1e-9L, dL);

        // time
        add_unit("min", 60.0L, dT);
        add_unit("h", 3600.0L, dT);
        add_unit("day", 86400.0L, dT);

        // derived units with correct dimensions (Newton: kg*m/s^2)
        Dimension dNdim = dM + dL + dT.pow_int(-2);
        add_unit("N", 1.0L, dNdim);
        Dimension Jdim = dNdim + dL; // N*m
        add_unit("J", 1.0L, Jdim);
        Dimension Padim = dNdim + dL.pow_int(-2); // N/m^2
        add_unit("Pa", 1.0L, Padim);
        Dimension Wdim = Jdim + dT.pow_int(-1);
        add_unit("W", 1.0L, Wdim);
        Dimension Hzdim = dT.pow_int(-1);
        add_unit("Hz", 1.0L, Hzdim);

        // energy units (common)
        add_unit("eV", 1.602176634e-19L, Jdim);

        // pressure
        add_unit("bar", 1e5L, Padim);
        add_unit("atm", 101325.0L, Padim);

        // length imperial
        add_unit("in", 0.0254L, dL);
        add_unit("ft", 0.3048L, dL);
        add_unit("yd", 0.9144L, dL);
        add_unit("mi", 1609.344L, dL);

        // mass imperial
        add_unit("lb", 0.45359237L, dM);
        add_unit("oz", 0.028349523125L, dM);

        // temperature: note Celsius conversion isn't multiplicative; using K-based only
        add_unit("degC", 1.0L, dTh); // placeholder (interpretation requires offset handling; for now token only)

        // angle
        Dimension ang; add_unit("rad", 1.0L, ang);
        add_unit("deg", M_PI/180.0L, ang);

        // others (convenience)
        add_unit("L", 0.001L, dL.pow_int(3)); // liter = 1e-3 m^3
    }

} UNIT_REG;

// ----------------- BigValue: numeric holder in SI units with dimension -----------------
struct BigValue {
    bool is_int;
    mpz_t i;   // valid if is_int
    mpfr_t f;  // valid if !is_int
    Dimension dim; // dimension expressed via the numeric value (SI scaled)
    // Note: unit label (like "m" or "km") not stored here; we keep canonical numeric in SI and dimension.

    BigValue() {
        is_int = true;
        mpz_init(i); mpz_set_ui(i, 0);
        mpfr_init2(f, DEFAULT_MPFR_PREC); mpfr_set_d(f, 0.0, MPFR_RNDN);
    }
    ~BigValue() {
        mpz_clear(i);
        mpfr_clear(f);
    }

    void set_from_string_and_unit(const string &numstr, const string &unitname) {
        string ns = trim(numstr);
        bool hasUnit = !unitname.empty();
        // Decide integer vs float based on presence of '.' or 'e' or 'E'
        bool looks_float = (ns.find_first_of(".eE") != string::npos);
        if (!hasUnit && !looks_float) {
            is_int = true;
            int rc = mpz_set_str(i, ns.c_str(), 10);
            if (rc != 0) {
                // fallback to float if mpz parsing fails
                is_int = false;
                mpfr_set_str(f, ns.c_str(), 10, MPFR_RNDN);
            }
        } else {
            is_int = false;
            mpfr_set_str(f, ns.c_str(), 10, MPFR_RNDN);
        }
        // apply unit factor (if any) to convert numeric to SI
        if (hasUnit) {
            UnitPtr u = UNIT_REG.lookup(unitname);
            if (!u) {
                // Try splitting prefixes: e.g., 'km' -> 'k' 'm' or metric prefixes map - but for now check simple prefixes
                // We'll attempt a basic split: find longest suffix that matches a unit
                UnitPtr found = nullptr;
                for (size_t pos = 1; pos < unitname.size(); ++pos) {
                    string suffix = unitname.substr(pos);
                    auto u2 = UNIT_REG.lookup(suffix);
                    if (u2) {
                        found = u2;
                        break;
                    }
                }
                if (!found) {
                    throw runtime_error("Unknown unit: " + unitname);
                } else {
                    u = found;
                }
            }
            // Multiply value by u->factor
            if (is_int) {
                // convert i to f then multiply factor
                mpfr_set_z(f, i, MPFR_RNDN);
                mpfr_mul(f, f, u->factor, MPFR_RNDN);
                is_int = false;
            } else {
                mpfr_mul(f, f, u->factor, MPFR_RNDN);
            }
            dim = u->dim;
        } else {
            // no unit: dimensionless (dim already zero)
        }
    }

    string to_human(bool prefer_si=false) const {
        // Prefer named unit whose factor gives a "nice" scaled numeric (0.1..1000) unless prefer_si is true
        long double approx = estimate_long_double();
        if (dim == Dimension()) {
            // dimensionless: print numeric
            if (is_int) {
                char *s = mpz_get_str(NULL, 10, i);
                string out(s); free(s);
                return out;
            } else {
                char *s = nullptr;
                mpfr_asprintf(&s, "%.12Rg", f);
                string out(s); mpfr_free_str(s);
                return out;
            }
        }
        // find candidates
        vector<UnitPtr> candidates = UNIT_REG.units_with_dim(dim);
        // prefer exact match name (like 'm' for dimension length)
        if (!prefer_si) {
            for (auto &u : candidates) {
                long double fac = mpfr_get_d(u->factor, MPFR_RNDN);
                if (fac == 0) continue;
                long double scaled = approx / fac;
                if (scaled >= 0.1L && scaled < 1000.0L) {
                    // format scaled nicely
                    ostringstream ss;
                    // choose integer if near integer
                    long double rounded = llround(scaled);
                    if (fabsl(scaled - rounded) < 1e-12L) ss << (long long)rounded << " " << u->name;
                    else {
                        // show with up to 12 significant digits
                        int sig = 12;
                        ss << setprecision(sig) << scaled << " " << u->name;
                    }
                    return ss.str();
                }
            }
        }
        // fallback: print SI value with compound unit
        // Get SI numeric
        ostringstream ss;
        if (is_int) {
            // but numeric stored in mpz is integer only if dimensionless. If not, we printed above; here convert to double approx
            long double v = estimate_long_double();
            ss << setprecision(12) << v;
        } else {
            char *s = nullptr;
            mpfr_asprintf(&s, "%.12Rg", f);
            ss << s;
            mpfr_free_str(s);
        }
        // append compound unit string like m^2*kg/s^2
        string unitstr = compound_unit_string(dim);
        if (!unitstr.empty()) ss << " " << unitstr;
        return ss.str();
    }

    long double estimate_long_double() const {
        if (is_int) {
            if (mpz_sgn(i) == 0) return 0.0L;
            // Use mpz_get_d gives double; may overflow to inf. To be robust, we extract first digits
            char *s = mpz_get_str(NULL, 10, i);
            string str(s); free(s);
            size_t take = min<size_t>(18, str.size());
            string lead = str.substr(0, take);
            long double lead_val = safe_stold(lead);
            long double frac = log10l(lead_val) - (long double)(take - 1);
            long double approx = powl(10.0L, (long double)(str.size() - 1) + frac);
            // this is an approximate magnitude; better than overflow
            return approx;
        } else {
            double d = mpfr_get_d(f, MPFR_RNDN);
            return (long double)d;
        }
    }

    long double estimate_log10() const {
        if (is_int) {
            if (mpz_sgn(i) == 0) return -INFINITY;
            // number of digits approx
            unsigned long digits = mpz_sizeinbase(i, 10);
            char *s = mpz_get_str(NULL, 10, i);
            string str(s); free(s);
            size_t take = min<size_t>(18, str.size());
            string lead = str.substr(0, take);
            long double lead_val = safe_stold(lead);
            long double frac = log10l(lead_val) - (long double)(take - 1);
            return (long double)(digits - 1) + frac;
        } else {
            if (mpfr_zero_p(f)) return -INFINITY;
            mpfr_t tmp; mpfr_init2(tmp, DEFAULT_MPFR_PREC);
            mpfr_log10(tmp, f, MPFR_RNDN);
            double d = mpfr_get_d(tmp, MPFR_RNDN);
            mpfr_clear(tmp);
            return (long double)d;
        }
    }

    static string compound_unit_string(const Dimension &dim) {
        // Build string like m^2*kg/s^2
        // We'll use UNIT_REG.baseNames mapping (index to base unit)
        // We'll produce numerator and denominator parts
        vector<string> num, den;
        // mapping indices -> names
        const vector<string> base = {"m", "kg", "s", "A", "K", "mol", "cd"};
        for (int i = 0; i < 7; ++i) {
            int p = dim.p[i];
            if (p == 0) continue;
            if (p > 0) {
                if (p == 1) num.push_back(base[i]);
                else num.push_back(base[i] + "^" + to_string(p));
            } else {
                int q = -p;
                if (q == 1) den.push_back(base[i]);
                else den.push_back(base[i] + "^" + to_string(q));
            }
        }
        ostringstream ss;
        if (num.empty()) ss << "1";
        else {
            for (size_t j = 0; j < num.size(); ++j) {
                if (j) ss << "*";
                ss << num[j];
            }
        }
        if (!den.empty()) {
            ss << "/";
            for (size_t j = 0; j < den.size(); ++j) {
                if (j) ss << "*";
                ss << den[j];
            }
        }
        return ss.str();
    }
};

// ----------------- Tokenizer & Shunting-yard -----------------
enum TokenType { T_NUM, T_IDENT, T_OP, T_LP, T_RP, T_TO };
struct Token {
    TokenType type;
    string text; // for numbers: maybe "123#unit" where we encode inline unit using '#'
};

static bool is_ident_char(char c) {
    return isalpha((unsigned char)c) || c == '_' || c == '/' || c == '%' || c == '.'; // allow deg symbol words (partial)
}

vector<Token> tokenize(const string &s) {
    vector<Token> out;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (isspace((unsigned char)c)) { ++i; continue; }
        if (isdigit(c) || (c == '.' && i + 1 < s.size() && isdigit(s[i + 1]))) {
            size_t j = i;
            bool seenE = false;
            while (j < s.size() && (isdigit(s[j]) || s[j] == '.' || s[j] == 'e' || s[j] == 'E' || ((s[j] == '+' || s[j] == '-') && j > i && (s[j - 1] == 'e' || s[j - 1] == 'E')))) j++;
            string num = s.substr(i, j - i);
            i = j;
            // optional inline unit immediately following digits/decimal (letters)
            size_t k = i;
            while (k < s.size() && is_ident_char(s[k])) k++;
            if (k > i) {
                string unit = s.substr(i, k - i);
                i = k;
                out.push_back({T_NUM, num + "#" + unit});
            } else {
                out.push_back({T_NUM, num});
            }
            continue;
        }
        if (is_ident_char(c)) {
            // read identifier (unit, or 'to', or function name)
            size_t j = i;
            while (j < s.size() && is_ident_char(s[j])) ++j;
            string id = s.substr(i, j - i);
            i = j;
            if (id == "to") out.push_back({T_TO, id});
            else out.push_back({T_IDENT, id});
            continue;
        }
        // operators and parens
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') { out.push_back({T_OP, string(1, c)}); ++i; continue; }
        if (c == '(') { out.push_back({T_LP, "("}); ++i; continue; }
        if (c == ')') { out.push_back({T_RP, ")"}); ++i; continue; }
        // unknown single char: treat as operator
        out.push_back({T_OP, string(1, c)});
        ++i;
    }
    return out;
}

int prec(const string &op) {
    if (op == "to") return 1;
    if (op == "=") return 1;
    if (op == "+" || op == "-") return 2;
    if (op == "*" || op == "/") return 3;
    if (op == "^") return 5;
    return 0;
}
bool right_assoc(const string &op) { return op == "^"; }

vector<Token> shunting_yard(const vector<Token> &tokens) {
    vector<Token> output;
    vector<Token> ops;
    for (size_t idx = 0; idx < tokens.size(); ++idx) {
        Token tk = tokens[idx];
        if (tk.type == T_NUM || tk.type == T_IDENT) {
            output.push_back(tk);
        } else if (tk.type == T_OP || tk.type == T_TO) {
            string op = tk.text;
            while (!ops.empty() && (ops.back().type == T_OP || ops.back().type == T_TO)) {
                string top = ops.back().text;
                if ((!right_assoc(op) && prec(op) <= prec(top)) || (right_assoc(op) && prec(op) < prec(top))) {
                    output.push_back(ops.back());
                    ops.pop_back();
                } else break;
            }
            ops.push_back(tk);
        } else if (tk.type == T_LP) {
            ops.push_back(tk);
        } else if (tk.type == T_RP) {
            bool found = false;
            while (!ops.empty()) {
                Token t = ops.back(); ops.pop_back();
                if (t.type == T_LP) { found = true; break; }
                output.push_back(t);
            }
            if (!found) throw runtime_error("Mismatched parentheses");
        } else {
            // ignore
        }
    }
    while (!ops.empty()) {
        Token t = ops.back(); ops.pop_back();
        if (t.type == T_LP || t.type == T_RP) throw runtime_error("Mismatched parentheses");
        output.push_back(t);
    }
    return output;
}

// ----------------- Evaluator with units and overflow-safe exponent -----------------
struct EvalConfig {
    long double max_digits = DEFAULT_MAX_DIGITS;
    int mpfr_prec = DEFAULT_MPFR_PREC;
    bool prefer_si = false;
};

static string approx_from_log10(long double log10v) {
    if (!isfinite(log10v)) return string("0");
    long double ip; long double frac = modfl(log10v, &ip);
    long double mant = powl(10.0L, frac);
    ostringstream ss; ss.setf(ios::scientific); ss.precision(9); ss << mant << "E" << (long long)ip;
    return ss.str();
}

// helper: pop stack; manage memory
static void free_stack(vector<BigValue*> &st) {
    for (auto p : st) delete p;
    st.clear();
}

// Convert Token.NUM text to BigValue: either "num" or "num#unit"
static BigValue* token_to_bigvalue(const Token &tk) {
    if (tk.type != T_NUM) throw runtime_error("Expected number token");
    string txt = tk.text;
    size_t pos = txt.find('#');
    string num = txt, unit = "";
    if (pos != string::npos) { num = txt.substr(0, pos); unit = txt.substr(pos + 1); }
    BigValue *v = new BigValue();
    v->set_from_string_and_unit(num, unit);
    return v;
}

// Evaluate RPN with unit handling and overflow detection
pair<bool, string> eval_rpn(const vector<Token> &rpn, const EvalConfig &cfg) {
    vector<BigValue*> st;
    try {
        for (size_t i = 0; i < rpn.size(); ++i) {
            Token tk = rpn[i];
            if (tk.type == T_NUM) {
                BigValue *v = token_to_bigvalue(tk);
                st.push_back(v);
            } else if (tk.type == T_IDENT) {
                // interpret identifier as a standalone unit (1 unit)
                string id = tk.text;
                BigValue *v = new BigValue();
                v->set_from_string_and_unit("1", id);
                st.push_back(v);
            } else if (tk.type == T_TO) {
                // binary operator: a to unit
                if (st.size() < 2) { free_stack(st); return {false, string("Error: 'to' requires left value and right unit identifier")}; }
                BigValue *unitv = st.back(); st.pop_back();
                BigValue *val = st.back(); st.pop_back();
                // unitv must be a unit identity: originally created from an identifier token => numeric 1 * unit factor
                // We need to find requested unit label: but tokenization pushed unit identifier as BigValue with numeric factor applied
                // To support robust 'to', we instead will accept pattern where the RIGHT operand is T_IDENT token in RPN.
                // So here fallback: if unitv.dim != dimensionless and unitv->estimate_long_double == factor of unit, we should map back to unit name.
                // Simpler: we won't attempt to recover the original token's name here; instead, when generating RPN, ensure that 'to' is encoded differently:
                // To keep single-file compatibility: we require syntax: '100 km to m' tokenizes to [100#km][m][to] where T_IDENT 'm' was pushed as token (not numeric).
                // So this code path may not be used. Instead, handle 'to' at RPN stage by reading previous tokens more carefully.
                // For safety, if unitv->dim != val->dim -> error. We'll attempt to find a unit in registry with the same dimension and whose factor equals unitv's numeric factor.
                // Compute numeric value of unitv (approx)
                // Find a unit name that matches same dimension and approximates factor; if none, error.
                // Convert val to requested unit: requiredFactor = unitFactor; current numeric is val (SI). result = numeric_in_SI / requiredFactor
                long double unit_factor_ld = unitv->estimate_long_double();
                Dimension ud = unitv->dim;
                // find candidate
                UnitPtr found = nullptr;
                for (auto &kv : UNIT_REG.table) {
                    UnitPtr u = kv.second;
                    if (!(u->dim == ud)) continue;
                    long double f = mpfr_get_d(u->factor, MPFR_RNDN);
                    if (fabsl(f - unit_factor_ld) / max((long double)1.0, fabsl(unit_factor_ld)) < 1e-12L) { found = u; break; }
                }
                if (!found) { free_stack(st); delete unitv; delete val; return {false, string("Error: unknown target unit for 'to'")}; }
                // compute val numeric in SI
                long double val_si = val->estimate_long_double();
                long double targetFactor = mpfr_get_d(found->factor, MPFR_RNDN);
                long double resultNumeric = val_si / targetFactor;
                // produce string
                ostringstream os; os<< std::fixed << std::setprecision(12); os << resultNumeric << " " << found->name;
                free_stack(st);
                delete unitv; delete val;
                return {false, os.str()};
            } else if (tk.type == T_OP) {
                string op = tk.text;
                if (op == "+") {
                    if (st.size() < 2) { free_stack(st); return {false, "Error: stack underflow +"}; }
                    BigValue *b = st.back(); st.pop_back();
                    BigValue *a = st.back(); st.pop_back();
                    if (!(a->dim == b->dim)) { free_stack(st); delete a; delete b; return {false, string("Error: Unit mismatch for +")} ; }
                    BigValue *r = new BigValue();
                    r->is_int = false;
                    mpfr_init2(r->f, cfg.mpfr_prec);
                    mpfr_t ta, tb; mpfr_init2(ta, cfg.mpfr_prec); mpfr_init2(tb, cfg.mpfr_prec);
                    if (a->is_int) mpfr_set_z(ta, a->i, MPFR_RNDN); else mpfr_set(ta, a->f, MPFR_RNDN);
                    if (b->is_int) mpfr_set_z(tb, b->i, MPFR_RNDN); else mpfr_set(tb, b->f, MPFR_RNDN);
                    mpfr_add(r->f, ta, tb, MPFR_RNDN);
                    mpfr_clear(ta); mpfr_clear(tb);
                    r->dim = a->dim;
                    st.push_back(r);
                    delete a; delete b;
                } else if (op == "-") {
                    if (st.size() < 2) { free_stack(st); return {false, "Error: stack underflow -"}; }
                    BigValue *b = st.back(); st.pop_back();
                    BigValue *a = st.back(); st.pop_back();
                    if (!(a->dim == b->dim)) { free_stack(st); delete a; delete b; return {false, string("Error: Unit mismatch for -")} ; }
                    BigValue *r = new BigValue();
                    r->is_int = false;
                    mpfr_init2(r->f, cfg.mpfr_prec);
                    mpfr_t ta, tb; mpfr_init2(ta, cfg.mpfr_prec); mpfr_init2(tb, cfg.mpfr_prec);
                    if (a->is_int) mpfr_set_z(ta, a->i, MPFR_RNDN); else mpfr_set(ta, a->f, MPFR_RNDN);
                    if (b->is_int) mpfr_set_z(tb, b->i, MPFR_RNDN); else mpfr_set(tb, b->f, MPFR_RNDN);
                    mpfr_sub(r->f, ta, tb, MPFR_RNDN);
                    mpfr_clear(ta); mpfr_clear(tb);
                    r->dim = a->dim;
                    st.push_back(r);
                    delete a; delete b;
                } else if (op == "*") {
                    if (st.size() < 2) { free_stack(st); return {false, "Error: stack underflow *"}; }
                    BigValue *b = st.back(); st.pop_back();
                    BigValue *a = st.back(); st.pop_back();
                    BigValue *r = new BigValue();
                    r->dim = a->dim + b->dim;
                    if (a->is_int && b->is_int && r->dim == Dimension()) {
                        // keep integer if dimensionless
                        mpz_init(r->i);
                        mpz_mul(r->i, a->i, b->i);
                        r->is_int = true;
                    } else {
                        r->is_int = false;
                        mpfr_init2(r->f, cfg.mpfr_prec);
                        mpfr_t ta, tb; mpfr_init2(ta, cfg.mpfr_prec); mpfr_init2(tb, cfg.mpfr_prec);
                        if (a->is_int) mpfr_set_z(ta, a->i, MPFR_RNDN); else mpfr_set(ta, a->f, MPFR_RNDN);
                        if (b->is_int) mpfr_set_z(tb, b->i, MPFR_RNDN); else mpfr_set(tb, b->f, MPFR_RNDN);
                        mpfr_mul(r->f, ta, tb, MPFR_RNDN);
                        mpfr_clear(ta); mpfr_clear(tb);
                    }
                    st.push_back(r);
                    delete a; delete b;
                } else if (op == "/") {
                    if (st.size() < 2) { free_stack(st); return {false, "Error: stack underflow /"}; }
                    BigValue *b = st.back(); st.pop_back();
                    BigValue *a = st.back(); st.pop_back();
                    BigValue *r = new BigValue();
                    r->dim = a->dim - b->dim;
                    r->is_int = false;
                    mpfr_init2(r->f, cfg.mpfr_prec);
                    mpfr_t ta, tb; mpfr_init2(ta, cfg.mpfr_prec); mpfr_init2(tb, cfg.mpfr_prec);
                    if (a->is_int) mpfr_set_z(ta, a->i, MPFR_RNDN); else mpfr_set(ta, a->f, MPFR_RNDN);
                    if (b->is_int) mpfr_set_z(tb, b->i, MPFR_RNDN); else mpfr_set(tb, b->f, MPFR_RNDN);
                    if (mpfr_zero_p(tb)) { free_stack(st); delete a; delete b; delete r; return {false, "Error: division by zero"}; }
                    mpfr_div(r->f, ta, tb, MPFR_RNDN);
                    mpfr_clear(ta); mpfr_clear(tb);
                    st.push_back(r);
                    delete a; delete b;
                } else if (op == "^") {
                    if (st.size() < 2) { free_stack(st); return {false, "Error: stack underflow ^"}; }
                    BigValue *expv = st.back(); st.pop_back();
                    BigValue *basev = st.back(); st.pop_back();
                    // exponent must be unitless (Dimension==0)
                    if (!(expv->dim == Dimension())) { free_stack(st); delete expv; delete basev; return {false, "Error: exponent must be unitless"}; }
                    // estimate log10(base) and magnitude of exponent
                    long double log10base = basev->estimate_log10();
                    long double exp_val_approx;
                    bool exp_is_int = expv->is_int;
                    unsigned long exp_ul = 0;
                    if (exp_is_int) {
                        // if exponent too big (lots of digits), produce approximation
                        unsigned long digits = mpz_sizeinbase(expv->i, 10);
                        if (digits > 18) {
                            // produce nested approx: base^(1E<digits-1>) as a readable fallback
                            string approx = basev->to_human(cfg.prefer_si) + string("^(1E") + to_string(digits - 1) + string(")");
                            free_stack(st); delete basev; delete expv;
                            return {true, approx};
                        }
                        exp_ul = mpz_get_ui(expv->i);
                        exp_val_approx = (long double)exp_ul;
                    } else {
                        // floating exponent: get approximate double
                        double dv = mpfr_get_d(expv->f, MPFR_RNDN);
                        exp_val_approx = (long double)dv;
                    }
                    // estimate log10(result) = exp * log10(base)
                    long double est_log10 = exp_val_approx * log10base;
                    if (!isfinite(est_log10) || est_log10 > cfg.max_digits) {
                        // overflow / huge result -> return approximate
                        string approx = approx_from_log10(est_log10);
                        free_stack(st); delete basev; delete expv;
                        return {true, approx};
                    }
                    // Try compute exactly if small
                    BigValue *res = new BigValue();
                    if (basev->is_int && exp_is_int && exp_ul <= 1000000UL) {
                        // integer power (capped)
                        mpz_pow_ui(res->i, basev->i, exp_ul);
                        res->is_int = true;
                        res->dim = basev->dim.pow_int((int)exp_ul);
                        st.push_back(res);
                        delete basev; delete expv;
                        continue;
                    } else {
                        // use mpfr pow via exp/log
                        res->is_int = false;
                        mpfr_init2(res->f, cfg.mpfr_prec);
                        mpfr_t tbase, texp, lbase;
                        mpfr_init2(tbase, cfg.mpfr_prec); mpfr_init2(texp, cfg.mpfr_prec); mpfr_init2(lbase, cfg.mpfr_prec);
                        if (basev->is_int) mpfr_set_z(tbase, basev->i, MPFR_RNDN); else mpfr_set(tbase, basev->f, MPFR_RNDN);
                        if (expv->is_int) mpfr_set_ui(texp, exp_ul, MPFR_RNDN); else mpfr_set(texp, expv->f, MPFR_RNDN);
                        mpfr_log(lbase, tbase, MPFR_RNDN);
                        mpfr_mul(lbase, lbase, texp, MPFR_RNDN);
                        mpfr_exp(res->f, lbase, MPFR_RNDN);
                        mpfr_clear(tbase); mpfr_clear(texp); mpfr_clear(lbase);
                        // dimension: if exponent was integer, raise dimension; if fractional it's approximate (not fully supported)
                        if (exp_is_int) res->dim = basev->dim.pow_int((int)exp_ul);
                        else res->dim = basev->dim; // approximate
                        st.push_back(res);
                        delete basev; delete expv;
                        continue;
                    }
                } else {
                    free_stack(st); return {false, string("Error: unknown operator '") + op + "'"};
                }
            } else {
                free_stack(st); return {false, string("Internal error: unexpected token in RPN")};
            }
        }
        if (st.size() != 1) { free_stack(st); return {false, string("Error: invalid expression (stack size ") + to_string(st.size()) + ")"}; }
        string out = st.back()->to_human(cfg.prefer_si);
        free_stack(st);
        return {false, out};
    } catch (const exception &e) {
        free_stack(st);
        return {false, string("Error: ") + e.what()};
    }
}

// ----------------- CLI and main -----------------
static void print_usage_and_exit(const char *prog) {
    cerr << "Usage: " << prog << " '<expression>' [--si] [--max-digits=N] [--precision=bits]\nExamples:\n  " << prog << " \"5 m + 12 cm\"\n  " << prog << " \"100 km to m\"\n";
    exit(1);
}

int main(int argc, char **argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if (argc < 2) print_usage_and_exit(argv[0]);
    string expr = argv[1];
    EvalConfig cfg;
    for (int i = 2; i < argc; ++i) {
        string a = argv[i];
        if (a.rfind("--max-digits=", 0) == 0) {
            cfg.max_digits = safe_stold(a.substr(12));
        } else if (a.rfind("--precision=", 0) == 0) {
            cfg.mpfr_prec = stoi(a.substr(12));
        } else if (a == "--si") {
            cfg.prefer_si = true;
        } else if (a == "--help" || a == "-h") {
            print_usage_and_exit(argv[0]);
        } else {
            cerr << "Unknown flag: " << a << "\n";
        }
    }

    vector<Token> tokens;
    try {
        tokens = tokenize(expr);
    } catch (const exception &e) {
        cerr << "Tokenize error: " << e.what() << "\n";
        return 1;
    }
    vector<Token> rpn;
    try {
        rpn = shunting_yard(tokens);
    } catch (const exception &e) {
        cerr << "Parse error: " << e.what() << "\n";
        return 1;
    }
    cout << "Processing (press Enter to abort)..\n";
    for (int i = 0; i < 10; ++i) {
        if (enter_pressed_nonblocking()) { cout << "Aborted.\n"; return 0; }
        usleep(CLI_ABORT_POLL_MS * 1000);
    }
    auto res = eval_rpn(rpn, cfg);
    if (res.first) {
        cout << "warning: Floating point overflow\n";
        cout << expr << " ≈ " << res.second << "\n";
    } else {
        cout << res.second << "\n";
    }
    return 0;
}
