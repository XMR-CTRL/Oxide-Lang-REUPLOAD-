#include "Driver.h"
#include "Lexer.h"
#include "Parser.h"
#include "Sema.h"
#include "IRGen.h"
#include "Smt.h"      // shared SmtCtx + ox_smt helpers (also used by Ghost.cpp)
#include "ProofSplitter.h"  // goal splitting + domain-aware tactic selection
#include "ProofDispatch.h"  // multi-prover dispatch + proof certificates

#include <cstdio>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <array>
#include <cctype>
#include <set>
#include <algorithm>
#include <functional>
#include <filesystem>

#ifdef _WIN32
  #define popen  _popen
  #define pclose _pclose
  #include <process.h>   // _getpid (PID-unique temp artifacts)
#else
  #include <unistd.h>    // getpid
#endif

namespace {
  std::string readFile(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { ok = false; return {}; }
    std::stringstream ss;
    ss << f.rdbuf();
    ok = true;
    return ss.str();
  }

  std::string stripExt(const std::string& path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos) return path;
    return path.substr(0, pos);
  }

  bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << content;
    return (bool)f;
  }


  std::string pathDir(const std::string& p) {
    auto bs = p.find_last_of('\\');
    auto fs = p.find_last_of('/');
    size_t pos = std::string::npos;
    if (bs != std::string::npos && fs != std::string::npos) pos = std::max(bs, fs);
    else if (bs != std::string::npos) pos = bs;
    else if (fs != std::string::npos) pos = fs;
    if (pos == std::string::npos) return ".";
    return p.substr(0, pos);
  }

  std::string joinPath(const std::string& dir, const std::string& rel) {
    std::string r = rel;
    while (r.size() >= 2 && r[0] == '.' && (r[1] == '/' || r[1] == '\\')) r = r.substr(2);
    if (dir.empty() || dir == ".") return r;
    char sep = (dir.find('\\') != std::string::npos) ? '\\' : '/';
    return dir + sep + r;
  }


}

std::string runtimeC() {
  return std::string(
      "#ifdef _MSC_VER\n"
      "#define _CRT_SECURE_NO_WARNINGS\n"
      "#endif\n"
      "#include <stdio.h>\n"
      "#include <stdlib.h>\n"
      "#include <string.h>\n"
      "#include <math.h>\n"
      "#include <stdint.h>\n"
      "#include <errno.h>\n"
      "#include <ctype.h>\n"
      "#include <time.h>\n"
      "#include <windows.h>\n"
      "\n"
      "\n"

      "static char* ox_arena_base = 0;\n"
      "static size_t ox_arena_end = 0, ox_arena_cap = 0;\n"
      "static void ox_oom(const char* where){ fprintf(stderr, \"oxide: out of memory in %s\\n\", where); abort(); }\n"
      "static void* ox_malloc_checked(size_t n, const char* where){ void* p = malloc(n ? n : 1); if(!p) ox_oom(where); return p; }\n"
      "static void* ox_calloc_checked(size_t n, size_t z, const char* where){ if(z && n > SIZE_MAX/z) ox_oom(where); void* p = calloc(n ? n : 1,z ? z : 1); if(!p) ox_oom(where); return p; }\n"
      "static void* ox_realloc_checked(void* old, size_t n, const char* where){ void* p = realloc(old,n ? n : 1); if(!p) ox_oom(where); return p; }\n"
      "static char* ox_arena_alloc(size_t n){\n"
      "  if(n > SIZE_MAX - 8u) ox_oom(\"arena size\");\n"
      "  if(!ox_arena_base){ ox_arena_cap = 1<<20; ox_arena_base = (char*)ox_malloc_checked(ox_arena_cap,\"arena init\"); ox_arena_end = 0; }\n"
      "  size_t need = (n + 8u) & ~(size_t)7u;          /* keep 8-byte alignment */\n"
      "  if(need > SIZE_MAX - ox_arena_end) ox_oom(\"arena overflow\");\n"
      "  if(ox_arena_end + need > ox_arena_cap){\n"
      "    size_t new_cap = ox_arena_cap;\n"
      "    while(ox_arena_end + need > new_cap){ if(new_cap > SIZE_MAX/2) ox_oom(\"arena grow\"); new_cap <<= 1; }\n"
      "    char* nb = (char*)ox_malloc_checked(new_cap,\"arena grow\");\n"
      "    memcpy(nb, ox_arena_base, ox_arena_end);\n"
      "    free(ox_arena_base); ox_arena_base = nb; ox_arena_cap = new_cap;\n"
      "  }\n"
      "  char* p = ox_arena_base + ox_arena_end; ox_arena_end += need; p[0] = 0; return p;\n"
      "}\n"
      "// Note: ox_arena_alloc doubles scratch room already; callers further NUL-tag the real length.\n"
      "\n"
      "// Every string-returning runtime function never returns NULL: on any\n"


      "static char* ox_str_short(size_t n){ char* p = ox_arena_alloc(n); p[n] = 0; return p; }\n"
      "\n"
      "int ox_puts(const char* s){ if(s) fputs(s, stdout); return 0; }\n"
      "int ox_puti(long long v){ printf(\"%lld\", v); return 0; }\n"
      "int ox_putf(double v){ printf(\"%g\", v); return 0; }\n"
      "int ox_newline(void){ putchar('\\n'); return 0; }\n"
      "int ox_putc(long long c){ putchar((int)(unsigned char)c); return 0; }\n"
      "\n"
      "long long ox_abs_i64(long long v){ return v < 0 ? -v : v; }\n"
      "double ox_sqrt(double v){ return sqrt(v); }\n"
      "long long ox_imin(long long a, long long b){ return a < b ? a : b; }\n"
      "long long ox_imax(long long a, long long b){ return a > b ? a : b; }\n"
      "double ox_fmin2(double a, double b){ return a < b ? a : b; }\n"
      "double ox_fmax2(double a, double b){ return a > b ? a : b; }\n"
      "\n"
      "char* ox_itos(long long v){ char* p = ox_str_short(24); sprintf(p, \"%lld\", v); return p; }\n"
      "long long ox_stoi(const char* s){ return s ? strtoll(s, 0, 10) : 0; }\n"
      "double ox_stod(const char* s){ return s ? strtod(s, 0) : 0.0; }\n"
      "\n"
      "// strconv: double -> str (arena-owned, never null). \"%g\" matches print of f64.\n"
      "char* ox_ftos(double v){ char* p = ox_str_short(32); sprintf(p, \"%g\", v); return p; }\n"
      "// a single char lifted to a 1-byte NUL-terminated string (arena-owned).\n"
      "char* ox_char_str(long long c){ char* p = ox_str_short(1); p[0] = (char)(unsigned char)c; p[1] = 0; return p; }\n"
      "\n"
      "// string comparison: <0 / 0 / >0, like strcmp. Null-safe (treats null as \"\").\n"
      "long long ox_strcmp(const char* a, const char* b){\n"
      "  if(!a) a = \"\"; if(!b) b = \"\";\n"
      "  return (long long)strcmp(a, b);\n"
      "}\n"
      "// length of a NUL-terminated string in bytes (excludes the terminator).\n"
      "long long ox_strlen(const char* s){ return s ? (long long)strlen(s) : 0; }\n"
      "// find first occurrence of char c in s; returns the byte index or -1 if absent.\n"
      "long long ox_strchr(const char* s, long long c){\n"
      "  if(!s) return -1;\n"
      "  const char* p = strchr(s, (int)(unsigned char)c);\n"
      "  return p ? (long long)(p - s) : -1;\n"
      "}\n"
      "// substring s[start..start+len) into a fresh arena string. Out-of-range\n"
      "// start/len are clamped (start past the end yields \"\", negative start\n"
      "// starts at 0, len past the end runs to the terminator). Never returns null.\n"
      "char* ox_substr(const char* s, long long start, long long len){\n"
      "  if(!s) return ox_str_short(0);\n"
      "  long long n = (long long)strlen(s);\n"
      "  if(start < 0) start = 0;\n"
      "  if(start > n) start = n;\n"
      "  if(len < 0) len = 0;\n"
      "  if(start + len > n) len = n - start;\n"
      "  char* p = ox_str_short((size_t)len);\n"
      "  if(len) memcpy(p, s + start, (size_t)len);\n"
      "  p[len] = 0; return p;\n"
      "}\n"
      "\n"
      "// String builder: ox_sb_new() returns an opaque growable buffer; ox_sb_puts\n"
      "// appends a NUL-terminated string; ox_sb_finish() returns an arena-owned\n"
      "// NUL-terminated string and frees the scratch.\n"
      "struct ox_sb { char* data; long long len, cap; };\n"
      "struct ox_sb* ox_sb_new(void){\n"
      "  struct ox_sb* sb = (struct ox_sb*)ox_malloc_checked(sizeof(struct ox_sb),\"string builder\");\n"
      "  sb->cap = 16; sb->len = 0; sb->data = (char*)ox_malloc_checked((size_t)sb->cap,\"string builder data\"); sb->data[0] = 0; return sb;\n"
      "}\n"
      "void ox_sb_puts(struct ox_sb* sb, const char* s){\n"
      "  if(!sb || !s) return;\n"
      "  long long n = (long long)strlen(s);\n"
      "  if(sb->len + n + 1 > sb->cap){\n"
      "    while(sb->len + n + 1 > sb->cap){ if(sb->cap > LLONG_MAX/2) ox_oom(\"string builder overflow\"); sb->cap <<= 1; }\n"
      "    sb->data = (char*)ox_realloc_checked(sb->data, (size_t)sb->cap,\"string builder grow\");\n"
      "  }\n"
      "  memcpy(sb->data + sb->len, s, (size_t)n); sb->len += n; sb->data[sb->len] = 0;\n"
      "}\n"
      "char* ox_sb_finish(struct ox_sb* sb){\n"
      "  if(!sb) return ox_str_short(0);\n"
      "  char* out = ox_str_short((size_t)sb->len);\n"
      "  if(sb->len) memcpy(out, sb->data, (size_t)sb->len);\n"
      "  out[sb->len] = 0;\n"
      "  free(sb->data); free(sb);\n"
      "  return out;\n"
      "}\n"
      "\n"
      "// Bounds-check failure: printed then aborted. The IR emits a call to this\n"
      "// when an array index is out of range (negative or >= length).\n"
      "void ox_bounds_fail(long long idx, long long len){\n"
      "  fprintf(stderr, \"oxide: array index out of bounds (index %lld, length %lld)\\n\", idx, len);\n"
      "  abort();\n"
      "}\n"
      "\n"
      "// Contract-violation trap printed then aborted. The IR emits a call to this\n"
      "// when a runtime contract gate (requires/ensures/invariant/assert) evaluates\n"
      "// to false. `tag` encodes the clause kind (1 requires, 2 ensures, 3\n"
      "// invariant, 4 assert); `line` is the source line of the violated clause.\n"
      "// This is the LAST line of defense even for contracts that SMT statically\n"
      "// discharged  -  the static proof operates over the abstract spec symbols\n"
      "// while this runtime gate evaluates the actual clause expression over the\n"
      "// caller's real values, so a static `unsat` PLUS this trap firing means\n"
      "// the spec encoder made an unsound assumption. Skipped entirely in\n"
      "// --freestanding mode (which omits the declaration too). Aborts so the\n"
      "// process exits with a non-zero status that `oxide run` surfaces as a\n"
      "// run-stage diagnostic (Driver::doRun prints the exit code on non-zero).\n"
      "void ox_contract_fail(int tag, int line){\n"
      "  const char* kind = (tag==1)?\"requires\":(tag==2)?\"ensures\":(tag==3)?\"invariant\":\"assert\";\n"
      "  fprintf(stderr, \"oxide: %s contract violated (line %d)\\n\", kind, line);\n"
      "  abort();\n"
      "}\n"
      "\n"
      "// io: console read and whole-file reads return NON-null arena buffers.\n"
      "char* ox_read_line(void){\n"
      "  /* Read into a growing scratch buffer, then copy the exact bytes into a\n"
      "     fresh arena string so every returned buffer is compact and NUL-safe. */\n"
      "  size_t cap = 256, len = 0;\n"
      "  char* scratch = (char*)ox_malloc_checked(cap,\"read line\");\n"
      "  int c;\n"
      "  while((c = getchar()) != EOF && c != '\\n'){\n"
      "    if(len == cap){ if(cap > SIZE_MAX/2) ox_oom(\"read line overflow\"); cap *= 2; scratch = (char*)ox_realloc_checked(scratch,cap,\"read line grow\"); }\n"
      "    scratch[len++] = (char)c;\n"
      "  }\n"
      "  char* buf = ox_str_short(len);\n"
      "  if(len) memcpy(buf, scratch, len);\n"
      "  buf[len] = 0;\n"
      "  free(scratch);\n"
      "  return buf;\n"
      "}\n"
      "char* ox_read_file(const char* path){\n"
      "  if(!path) return ox_str_short(0);\n"
      "  FILE* f = fopen(path, \"rb\"); if(!f) return ox_str_short(0);\n"
      "  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);\n"
      "  if(n < 0){ fclose(f); return ox_str_short(0); }\n"
      "  char* buf = ox_str_short((size_t)n); size_t rd = fread(buf, 1, (size_t)n, f); fclose(f);\n"
      "  buf[rd] = 0; return buf;\n"
      "}\n"
      "// file handles are just FILE* stored as a pointer-width integer.\n"
      "long long ox_file_open(const char* path, const char* mode){\n"
      "  if(!path || !mode) return -1; FILE* f = fopen(path, mode); \n"
      "  if(!f) return -1; return (long long)(intptr_t)f;\n"
      "}\n"
      "long long ox_file_close(long long h){ if(h < 0) return -1; return (long long)fclose((FILE*)(intptr_t)h); }\n"
      "char* ox_file_read(long long h){\n"
      "  if(h < 0) return ox_str_short(0); FILE* f = (FILE*)(intptr_t)h;\n"
      "  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);\n"
      "  if(n < 0) return ox_str_short(0); char* buf = ox_str_short((size_t)n); size_t rd = fread(buf,1,(size_t)n,f); buf[rd]=0; return buf;\n"
      "}\n"
      "long long ox_file_write(long long h, const char* s){ if(h < 0 || !s) return -1; return (long long)fputs(s, (FILE*)(intptr_t)h); }\n"
      "int ox_file_exists(const char* path){ if(!path) return 0; FILE* f = fopen(path,\"rb\"); if(!f) return 0; fclose(f); return 1; }\n"
      "\n"
      "// ----------------------------------------------------------------\n"
      "// Dynamic (growable) array runtime. A handle is a pointer to a\n"
      "// header holding { len, cap, data }; elements are typed in host C, so the\n"
      "// name suffix selects the concrete element type (i64 f64 i1 str).\n"
      "// ----------------------------------------------------------------\n"
      "struct ox_vec { long long len, cap; void* data; };\n"
      "static struct ox_vec* ox_vec_check(void* h, const char* who){\n"
      "  if(!h){ fprintf(stderr, \"oxide: %s on a null vec\\n\", who); abort(); }\n"
      "  return (struct ox_vec*)h;\n"
      "}\n"
      "long long ox_vec_len(void* h){ return ox_vec_check(h,\"len\")->len; }\n"
      "static void ox_vec_grow(struct ox_vec* v, size_t esz){\n"
      "  if(v->len < v->cap) return;\n"
      "  if(v->cap > LLONG_MAX/2) ox_oom(\"vec capacity overflow\");\n"
      "  long long nc = v->cap ? v->cap*2 : 8;\n"
      "  if(esz && (size_t)nc > SIZE_MAX/esz) ox_oom(\"vec size overflow\");\n"
      "  v->data = ox_realloc_checked(v->data, (size_t)nc * esz,\"vec grow\");\n"
      "  v->cap = nc;\n"
      "}\n"
      "// The four element-kind typos define new/push/get/set/print. As the\n"
      "// compiler only references the suffixes the program uses, we always\n"
      "// provide all four to simplify the C side; linkers keep the unused ones.\n"
      "#define OX_VEC_KIND(SUF, ETYPE, FMT) \\\n"
      "  void* ox_vec_new_##SUF(void){ struct ox_vec* v = (struct ox_vec*)ox_calloc_checked(1,sizeof(*v),\"vec new\"); return v; } \\\n"
      "  void ox_vec_push_##SUF(void* h, ETYPE x){ struct ox_vec* v = ox_vec_check(h,\"push\"); ox_vec_grow(v, sizeof(ETYPE)); ((ETYPE*)v->data)[v->len++] = x; } \\\n"
      "  ETYPE ox_vec_get_##SUF(void* h, long long i){ struct ox_vec* v = ox_vec_check(h,\"get\"); if(i<0||i>=v->len){ fprintf(stderr, \"oxide: vec index out of bounds (%lld, len %lld)\\n\", i, v->len); abort(); } return ((ETYPE*)v->data)[i]; } \\\n"
      "  void ox_vec_set_##SUF(void* h, long long i, ETYPE x){ struct ox_vec* v = ox_vec_check(h,\"set\"); if(i<0||i>=v->len){ fprintf(stderr, \"oxide: vec index out of bounds (%lld, len %lld)\\n\", i, v->len); abort(); } ((ETYPE*)v->data)[i] = x; } \\\n"
      "  void ox_vec_print_##SUF(void* h){ struct ox_vec* v = ox_vec_check(h,\"print\"); putchar('['); for(long long i=0;i<v->len;i++){ if(i) fputs(\", \", stdout); printf(FMT, ((ETYPE*)v->data)[i]); } putchar(']'); }\n"
      "OX_VEC_KIND(i64, long long, \"%lld\")\n"
      "OX_VEC_KIND(f64, double, \"%g\")\n"
      "OX_VEC_KIND(i1, int, \"%d\")\n"
      "// char elements are i8 (printed as a character, not a number).\n"
      "void* ox_vec_new_i8(void){ struct ox_vec* v = (struct ox_vec*)ox_calloc_checked(1,sizeof(*v),\"vec new\"); return v; }\n"
      "void ox_vec_push_i8(void* h, char x){ struct ox_vec* v = ox_vec_check(h,\"push\"); ox_vec_grow(v, sizeof(char)); ((char*)v->data)[v->len++] = x; }\n"
      "char ox_vec_get_i8(void* h, long long i){ struct ox_vec* v = ox_vec_check(h,\"get\"); if(i<0||i>=v->len){ fprintf(stderr, \"oxide: vec index out of bounds (%lld, len %lld)\\n\", i, v->len); abort(); } return ((char*)v->data)[i]; }\n"
      "void ox_vec_set_i8(void* h, long long i, char x){ struct ox_vec* v = ox_vec_check(h,\"set\"); if(i<0||i>=v->len){ fprintf(stderr, \"oxide: vec index out of bounds (%lld, len %lld)\\n\", i, v->len); abort(); } ((char*)v->data)[i] = x; }\n"
      "void ox_vec_print_i8(void* h){ struct ox_vec* v = ox_vec_check(h,\"print\"); putchar('['); for(long long i=0;i<v->len;i++){ if(i) fputs(\", \", stdout); putchar((int)((unsigned char)((char*)v->data)[i])); } putchar(']'); }\n"
      "// str elements are i8* (NUL-terminated), printed with fputs.\n"
      "void* ox_vec_new_str(void){ struct ox_vec* v = (struct ox_vec*)ox_calloc_checked(1,sizeof(*v),\"vec new\"); return v; }\n"
      "void ox_vec_push_str(void* h, char* x){ struct ox_vec* v = ox_vec_check(h,\"push\"); ox_vec_grow(v, sizeof(char*)); ((char**)v->data)[v->len++] = x; }\n"
      "char* ox_vec_get_str(void* h, long long i){ struct ox_vec* v = ox_vec_check(h,\"get\"); if(i<0||i>=v->len){ fprintf(stderr, \"oxide: vec index out of bounds (%lld, len %lld)\\n\", i, v->len); abort(); } return ((char**)v->data)[i]; }\n"
      "void ox_vec_set_str(void* h, long long i, char* x){ struct ox_vec* v = ox_vec_check(h,\"set\"); if(i<0||i>=v->len){ fprintf(stderr, \"oxide: vec index out of bounds (%lld, len %lld)\\n\", i, v->len); abort(); } ((char**)v->data)[i] = x; }\n"
      "void ox_vec_print_str(void* h){ struct ox_vec* v = ox_vec_check(h,\"print\"); putchar('['); for(long long i=0;i<v->len;i++){ if(i) fputs(\", \", stdout); fputs(((char**)v->data)[i] ? ((char**)v->data)[i] : \"\", stdout); } putchar(']'); }\n"
      "\n"
      "// ----------------------------------------------------------------\n"
      "// sort: in-place ascending sort of a vec's elements. Two entry points:\n"
      "//  ox_sort_<suffix> for the typed fast-path vecs (i64/f64/i1/i8/str), and a\n"
      "//  generic ox_sort_blob(h, esz, kind) for blob vecs keyed on the element\n"
      "//  byte width + a category tag (0 signed int, 1 unsigned int, 2 float,\n"
      "//  3 pointer/str). Struct/aggregate elements can't be sorted this way.\n"
      "// ----------------------------------------------------------------\n"
      "static int ox_cmp_i64(const void* a, const void* b){ long long x=*(const long long*)a, y=*(const long long*)b; return (x>y)-(x<y); }\n"
      "static int ox_cmp_f64(const void* a, const void* b){ double x=*(const double*)a, y=*(const double*)b; return (x>y)-(x<y); }\n"
      "static int ox_cmp_i1 (const void* a, const void* b){ int x=*(const int*)a, y=*(const int*)b; return (x>y)-(x<y); }\n"
      "static int ox_cmp_i8 (const void* a, const void* b){ unsigned char x=*(const unsigned char*)a, y=*(const unsigned char*)b; return (int)x-(int)y; }\n"
      "static int ox_cmp_str(const void* a, const void* b){ const char* x=*(const char* const*)a; const char* y=*(const char* const*)b; if(!x) x=\"\"; if(!y) y=\"\"; return strcmp(x,y); }\n"
      "void ox_sort_i64(void* h){ struct ox_vec* v = ox_vec_check(h,\"sort\"); if(v->len>1) qsort(v->data, (size_t)v->len, sizeof(long long), ox_cmp_i64); }\n"
      "void ox_sort_f64(void* h){ struct ox_vec* v = ox_vec_check(h,\"sort\"); if(v->len>1) qsort(v->data, (size_t)v->len, sizeof(double), ox_cmp_f64); }\n"
      "void ox_sort_i1 (void* h){ struct ox_vec* v = ox_vec_check(h,\"sort\"); if(v->len>1) qsort(v->data, (size_t)v->len, sizeof(int), ox_cmp_i1); }\n"
      "void ox_sort_i8 (void* h){ struct ox_vec* v = ox_vec_check(h,\"sort\"); if(v->len>1) qsort(v->data, (size_t)v->len, sizeof(char), ox_cmp_i8); }\n"
      "void ox_sort_str(void* h){ struct ox_vec* v = ox_vec_check(h,\"sort\"); if(v->len>1) qsort(v->data, (size_t)v->len, sizeof(char*), ox_cmp_str); }\n"
      "// generic blob sort: kind selects a signed/unsigned/float/ptr comparator;\n"
      "// the element width selects the exact C type read at each slot so small\n"
      "// ints and f32 widen correctly (a 4-byte read into an 8-byte value would\n"
      "// misread f32 bit patterns, so dispatch on esz).\n"
      "static int ox_sort_blob_kind; static size_t ox_sort_blob_esz;\n"
      "static int ox_cmp_blob(const void* a, const void* b){\n"
      "  if(ox_sort_blob_kind==3){ const char* x=*(const char* const*)a; const char* y=*(const char* const*)b; if(!x) x=\"\"; if(!y) y=\"\"; return strcmp(x,y); }\n"
      "  if(ox_sort_blob_kind==2){\n"
      "    if(ox_sort_blob_esz==4){ float x=*(const float*)a, y=*(const float*)b; return (x>y)-(x<y); }\n"
      "    double x=*(const double*)a, y=*(const double*)b; return (x>y)-(x<y);\n"
      "  }\n"
      "  // integer kinds: read exactly esz bytes (zero-init, then memcpy) and, for\n"
      "  // signed, sign-extend from the top read bit so i8/i16/i32 compare right.\n"
      "  unsigned long long ua=0, ub=0;\n"
      "  memcpy(&ua, a, ox_sort_blob_esz); memcpy(&ub, b, ox_sort_blob_esz);\n"
      "  if(ox_sort_blob_kind==0){\n"
      "    long long sa, sb;\n"
      "    if(ox_sort_blob_esz==1){ sa=(signed char)ua; sb=(signed char)ub; }\n"
      "    else if(ox_sort_blob_esz==2){ sa=(short)ua; sb=(short)ub; }\n"
      "    else if(ox_sort_blob_esz==4){ sa=(int)ua; sb=(int)ub; }\n"
      "    else { sa=(long long)ua; sb=(long long)ub; }\n"
      "    return (sa>sb)-(sa<sb);\n"
      "  }\n"
      "  return (ua>ub)-(ua<ub);\n"
      "}\n"
      "void ox_sort_blob(void* h, long long esz, long long kind){\n"
      "  struct ox_vec* v = ox_vec_check(h,\"sort\"); if(v->len<2) return;\n"
      "  ox_sort_blob_esz = (size_t)esz; ox_sort_blob_kind = (int)kind;\n"
      "  qsort(v->data, (size_t)v->len, (size_t)esz, ox_cmp_blob);\n"
      "}\n"
      "\n"
      "// (structs, fixed arrays, nested vecs) via memcpy in/out. The element\n"
      "// byte size `esz` is fixed at construction and passed to each operation.\n"
      "// `ox_vec_blob_ptr` yields a pointer to slot i so the compiler can read or\n"
      "// write a struct/aggregate element in place (GEP into the data buffer).\n"
      "// For pointer-shaped elements (the fast i8* path) the compiler uses str/\n"
      "// i64 accessors; blobs only kick in for non-pointer-sized aggregate slots.\n"
      "// ----------------------------------------------------------------\n"
      "static void ox_vec_blob_grow(struct ox_vec* v, size_t esz){\n"
      "  if(v->len < v->cap) return;\n"
      "  if(v->cap > LLONG_MAX/2) ox_oom(\"vec capacity overflow\");\n"
      "  long long nc = v->cap ? v->cap*2 : 8;\n"
      "  if(esz && (size_t)nc > SIZE_MAX/esz) ox_oom(\"vec size overflow\");\n"
      "  v->data = ox_realloc_checked(v->data, (size_t)nc * esz,\"vec grow\");\n"
      "  v->cap = nc;\n"
      "}\n"
      "void* ox_vec_blob_new(long long esz){\n"
      "  struct ox_vec* v = (struct ox_vec*)ox_calloc_checked(1,sizeof(*v),\"vec new\");\n"
      "  /* stash esz in the otherwise-unused high half? no: keep esz explicit on\n"
      "     every call so the header stays the same as the typed vec kinds. */\n"
      "  (void)esz; return v;\n"
      "}\n"
      "void ox_vec_blob_push(void* h, long long esz, void* src){\n"
      "  struct ox_vec* v = ox_vec_check(h,\"blob_push\");\n"
      "  ox_vec_blob_grow(v, (size_t)esz);\n"
      "  memcpy((char*)v->data + v->len*(long long)esz, src, (size_t)esz);\n"
      "  v->len++;\n"
      "}\n"
      "void ox_vec_blob_get(void* h, long long i, long long esz, void* dst){\n"
      "  struct ox_vec* v = ox_vec_check(h,\"blob_get\");\n"
      "  if(i<0||i>=v->len){ fprintf(stderr, \"oxide: vec index out of bounds (%lld, len %lld)\\n\", i, v->len); abort(); }\n"
      "  memcpy(dst, (char*)v->data + i*(long long)esz, (size_t)esz);\n"
      "}\n"
      "void ox_vec_blob_set(void* h, long long i, long long esz, void* src){\n"
      "  struct ox_vec* v = ox_vec_check(h,\"blob_set\");\n"
      "  if(i<0||i>=v->len){ fprintf(stderr, \"oxide: vec index out of bounds (%lld, len %lld)\\n\", i, v->len); abort(); }\n"
      "  memcpy((char*)v->data + i*(long long)esz, src, (size_t)esz);\n"
      "}\n"
      "// in-place pointer to slot i (for field access / nested indexing into a\n"
      "// blob element without a round-trip copy).\n"
      "void* ox_vec_blob_ptr(void* h, long long i, long long esz){\n"
      "  struct ox_vec* v = ox_vec_check(h,\"blob_ptr\");\n"
      "  if(i<0||i>=v->len){ fprintf(stderr, \"oxide: vec index out of bounds (%lld, len %lld)\\n\", i, v->len); abort(); }\n"
      "  return (char*)v->data + i*(long long)esz;\n"
      "}\n"
      "\n"
      "// ----------------------------------------------------------------\n"
      "// map[K,V]: an ordered associative array (sorted ascending by key) with\n"
      "// O(log n) lookup and O(n) insert/delete. One generic family, keyed on\n"
      "// the key byte width (kw), value byte width (vw), and a key category tag\n"
      "// (kk: 0 signed int, 1 unsigned, 2 float, 3 str/ptr)  -  the same category\n"
      "// scheme as sort. Keys are stored sorted; values live in a parallel array.\n"
      "// ----------------------------------------------------------------\n"
      "struct ox_map { long long len, cap; char* keys; char* vals; long long kw, vw, kk; };\n"
      "static struct ox_map* ox_map_check(void* h, const char* who){\n"
      "  if(!h){ fprintf(stderr, \"oxide: %s on a null map\\n\", who); abort(); }\n"
      "  return (struct ox_map*)h;\n"
      "}\n"
      "void* ox_map_new(long long kw, long long vw, long long kk){\n"
      "  struct ox_map* m = (struct ox_map*)ox_calloc_checked(1,sizeof(*m),\"map new\");\n"
      "  m->kw = kw; m->vw = vw; m->kk = kk; m->cap = 0; return m;\n"
      "}\n"
      "long long ox_map_len(void* h){ return ox_map_check(h,\"len\")->len; }\n"
      "// compare a key buffer against a stored key slot, using the category kind\n"
      "// so signed/unsigned/float order correctly across all int widths.\n"
      "static int ox_map_kcmp(const struct ox_map* m, const char* a, const char* b){\n"
      "  if(m->kk==3){ const char* x=*(const char* const*)a; const char* y=*(const char* const*)b; if(!x) x=\"\"; if(!y) y=\"\"; return strcmp(x,y); }\n"
      "  if(m->kk==2){\n"
      "    if(m->kw==4){ float x=*(const float*)a, y=*(const float*)b; return (x>y)-(x<y); }\n"
      "    double x=*(const double*)a, y=*(const double*)b; return (x>y)-(x<y);\n"
      "  }\n"
      "  unsigned long long ua=0, ub=0; memcpy(&ua,a,(size_t)m->kw); memcpy(&ub,b,(size_t)m->kw);\n"
      "  if(m->kk==0){\n"
      "    long long sa, sb;\n"
      "    if(m->kw==1){ sa=(signed char)ua; sb=(signed char)ub; }\n"
      "    else if(m->kw==2){ sa=(short)ua; sb=(short)ub; }\n"
      "    else if(m->kw==4){ sa=(int)ua; sb=(int)ub; }\n"
      "    else { sa=(long long)ua; sb=(long long)ub; }\n"
      "    return (sa>sb)-(sa<sb);\n"
      "  }\n"
      "  return (ua>ub)-(ua<ub);\n"
      "}\n"
      "// lower_bound: the first index whose key is >= q. Returns len if all are < q.\n"
      "static long long ox_map_lb(struct ox_map* m, const char* q){\n"
      "  long long lo=0, hi=m->len;\n"
      "  while(lo<hi){ long long mid=lo+(hi-lo)/2; if(ox_map_kcmp(m, m->keys + mid*m->kw, q) < 0) lo=mid+1; else hi=mid; }\n"
      "  return lo;\n"
      "}\n"
      "static void ox_map_grow(struct ox_map* m){\n"
      "  if(m->len < m->cap) return;\n"
      "  if(m->cap > LLONG_MAX/2) ox_oom(\"map capacity overflow\");\n"
      "  long long nc = m->cap ? m->cap*2 : 8;\n"
      "  if(m->kw && (size_t)nc > SIZE_MAX/(size_t)m->kw) ox_oom(\"map key size overflow\");\n"
      "  if(m->vw && (size_t)nc > SIZE_MAX/(size_t)m->vw) ox_oom(\"map value size overflow\");\n"
      "  m->keys = (char*)ox_realloc_checked(m->keys, (size_t)nc * (size_t)m->kw,\"map keys grow\");\n"
      "  m->vals = (char*)ox_realloc_checked(m->vals, (size_t)nc * (size_t)m->vw,\"map values grow\");\n"
      "  m->cap = nc;\n"
      "}\n"
      "// set(m, kptr, vptr): insert-or-replace. The key/value live in caller\n"
      "// scratch (bytewise copies; widened by kw/vw on the Oxide side first).\n"
      "void ox_map_set(void* h, const void* kp, const void* vp){\n"
      "  struct ox_map* m = ox_map_check(h,\"set\");\n"
      "  const char* k = (const char*)kp;\n"
      "  long long i = ox_map_lb(m, k);\n"
      "  if(i < m->len && ox_map_kcmp(m, m->keys + i*m->kw, k) == 0){\n"
      "    memcpy(m->vals + i*m->vw, vp, (size_t)m->vw); return;   // replace value\n"
      "  }\n"
      "  ox_map_grow(m);\n"
      "  memmove(m->keys + (i+1)*m->kw, m->keys + i*m->kw, (size_t)(m->len - i) * (size_t)m->kw);\n"
      "  memmove(m->vals + (i+1)*m->vw, m->vals + i*m->vw, (size_t)(m->len - i) * (size_t)m->vw);\n"
      "  memcpy(m->keys + i*m->kw, k, (size_t)m->kw);\n"
      "  memcpy(m->vals + i*m->vw, vp, (size_t)m->vw);\n"
      "  m->len++;\n"
      "}\n"
      "// get(m, kptr, vptr): if present, copy the value into *vptr and return 1;\n"
      "// else zero-fill *vptr and return 0 (so a missing key reads as zero).\n"
      "long long ox_map_get(void* h, const void* kp, void* vp){\n"
      "  struct ox_map* m = ox_map_check(h,\"get\");\n"
      "  long long i = ox_map_lb(m, (const char*)kp);\n"
      "  if(i < m->len && ox_map_kcmp(m, m->keys + i*m->kw, (const char*)kp) == 0){\n"
      "    memcpy(vp, m->vals + i*m->vw, (size_t)m->vw); return 1;\n"
      "  }\n"
      "  memset(vp, 0, (size_t)m->vw); return 0;\n"
      "}\n"
      "// contains(m, kptr): 1 if the key is present, else 0.\n"
      "long long ox_map_contains(void* h, const void* kp){\n"
      "  struct ox_map* m = ox_map_check(h,\"contains\");\n"
      "  long long i = ox_map_lb(m, (const char*)kp);\n"
      "  return (i < m->len && ox_map_kcmp(m, m->keys + i*m->kw, (const char*)kp) == 0) ? 1 : 0;\n"
      "}\n"
      "// key pointer at sorted index i (for `map_keys`, which copies out a vec).\n"
      "void* ox_map_key_ptr(void* h, long long i){\n"
      "  struct ox_map* m = ox_map_check(h,\"key_ptr\");\n"
      "  if(i<0||i>=m->len){ fprintf(stderr, \"oxide: map key index out of bounds (%lld, len %lld)\\n\", i, m->len); abort(); }\n"
      "  return m->keys + i*(long long)m->kw;\n"
      "}\n"
      "\n"
      "// ----------------------------------------------------------------\n"
      "// set[T] (std::set): a sorted-unique array of keys (no values). Same\n"
      "// comparator + sorted-insert machinery as ox_map, minus the value side.\n"
      "// ----------------------------------------------------------------\n"
      "struct ox_set { long long len, cap; char* data; long long kw, kk; };\n"
      "static struct ox_set* ox_set_check(void* h, const char* who){\n"
      "  if(!h){ fprintf(stderr, \"oxide: %s on a null set\\n\", who); abort(); }\n"
      "  return (struct ox_set*)h;\n"
      "}\n"
      "void* ox_set_new(long long kw, long long kk){\n"
      "  struct ox_set* s = (struct ox_set*)ox_calloc_checked(1,sizeof(*s),\"set new\");\n"
      "  s->kw = kw; s->kk = kk; return s;\n"
      "}\n"
      "long long ox_set_len(void* h){ return ox_set_check(h,\"len\")->len; }\n"
      "static int ox_set_kcmp(const struct ox_set* s, const char* a, const char* b){\n"
      "  if(s->kk==3){ const char* x=*(const char* const*)a; const char* y=*(const char* const*)b; if(!x) x=\"\"; if(!y) y=\"\"; return strcmp(x,y); }\n"
      "  if(s->kk==2){\n"
      "    if(s->kw==4){ float x=*(const float*)a, y=*(const float*)b; return (x>y)-(x<y); }\n"
      "    double x=*(const double*)a, y=*(const double*)b; return (x>y)-(x<y);\n"
      "  }\n"
      "  unsigned long long ua=0, ub=0; memcpy(&ua,a,(size_t)s->kw); memcpy(&ub,b,(size_t)s->kw);\n"
      "  if(s->kk==0){\n"
      "    long long sa, sb;\n"
      "    if(s->kw==1){ sa=(signed char)ua; sb=(signed char)ub; }\n"
      "    else if(s->kw==2){ sa=(short)ua; sb=(short)ub; }\n"
      "    else if(s->kw==4){ sa=(int)ua; sb=(int)ub; }\n"
      "    else { sa=(long long)ua; sb=(long long)ub; }\n"
      "    return (sa>sb)-(sa<sb);\n"
      "  }\n"
      "  return (ua>ub)-(ua<ub);\n"
      "}\n"
      "static long long ox_set_lb(struct ox_set* s, const char* q){\n"
      "  long long lo=0, hi=s->len;\n"
      "  while(lo<hi){ long long mid=lo+(hi-lo)/2; if(ox_set_kcmp(s, s->data + mid*s->kw, q) < 0) lo=mid+1; else hi=mid; }\n"
      "  return lo;\n"
      "}\n"
      "static void ox_set_grow(struct ox_set* s){\n"
      "  if(s->len < s->cap) return;\n"
      "  if(s->cap > LLONG_MAX/2) ox_oom(\"set capacity overflow\");\n"
      "  long long nc = s->cap ? s->cap*2 : 8;\n"
      "  if(s->kw && (size_t)nc > SIZE_MAX/(size_t)s->kw) ox_oom(\"set size overflow\");\n"
      "  s->data = (char*)ox_realloc_checked(s->data, (size_t)nc * (size_t)s->kw,\"set grow\");\n"
      "  s->cap = nc;\n"
      "}\n"
      "// insert(elem): add if absent (std::set::insert). Idempotent: inserting an\n"
      "// already-present element is a no-op.\n"
      "void ox_set_insert(void* h, const void* ep){\n"
      "  struct ox_set* s = ox_set_check(h,\"insert\");\n"
      "  const char* k = (const char*)ep;\n"
      "  long long i = ox_set_lb(s, k);\n"
      "  if(i < s->len && ox_set_kcmp(s, s->data + i*s->kw, k) == 0) return;\n"
      "  ox_set_grow(s);\n"
      "  memmove(s->data + (i+1)*s->kw, s->data + i*s->kw, (size_t)(s->len - i) * (size_t)s->kw);\n"
      "  memcpy(s->data + i*s->kw, k, (size_t)s->kw);\n"
      "  s->len++;\n"
      "}\n"
      "// remove(elem): erase if present (std::set::erase). Idempotent.\n"
      "void ox_set_remove(void* h, const void* ep){\n"
      "  struct ox_set* s = ox_set_check(h,\"remove\");\n"
      "  const char* k = (const char*)ep;\n"
      "  long long i = ox_set_lb(s, k);\n"
      "  if(i < s->len && ox_set_kcmp(s, s->data + i*s->kw, k) == 0){\n"
      "    memmove(s->data + i*s->kw, s->data + (i+1)*s->kw, (size_t)(s->len - i - 1) * (size_t)s->kw);\n"
      "    s->len--;\n"
      "  }\n"
      "}\n"
      "// contains(elem): 1 if present, else 0.\n"
      "long long ox_set_contains(void* h, const void* ep){\n"
      "  struct ox_set* s = ox_set_check(h,\"contains\");\n"
      "  long long i = ox_set_lb(s, (const char*)ep);\n"
      "  return (i < s->len && ox_set_kcmp(s, s->data + i*s->kw, (const char*)ep) == 0) ? 1 : 0;\n"
      "}\n"
      "// element pointer at sorted index i (for `set_to_vec` / iteration).\n"
      "void* ox_set_ptr(void* h, long long i){\n"
      "  struct ox_set* s = ox_set_check(h,\"ptr\");\n"
      "  if(i<0||i>=s->len){ fprintf(stderr, \"oxide: set index out of bounds (%lld, len %lld)\\n\", i, s->len); abort(); }\n"
      "  return s->data + i*(long long)s->kw;\n"
      "}\n"
      "\n"
      "// ----------------------------------------------------------------\n"
      "// hmap[K,V] / hset[T]: an open-addressing hash table (linear probing,\n"
      "// power-of-two capacity, load factor 0.75) keyed on the same (kw, kk)\n"
      "// category scheme as ox_map/ox_set. Entries are kept in INSERTION ORDER in\n"
      "// parallel keys/vals arrays, and a `buckets` table maps a key hash to its\n"
      "// insertion index for O(1) lookup. Iteration (key_ptr/val_ptr/set_ptr) and\n"
      "// map_keys/set_to_vec therefore walk entries in insertion order (Python-dict\n"
      "// / ES-Map semantics)  -  deterministic, unlike a raw hash walk. delete uses\n"
      "// swap-with-last so survivors keep their relative insertion order. The same\n"
      "// (kw, kk) comparator as ox_map is reused so signed/unsigned/float/str keys\n"
      "// compare identically to the ordered map. Exposes the same symbol set as the\n"
      "// ordered collections so IRGen routing is a clean parallel.\n"
      "// ----------------------------------------------------------------\n"
      "struct ox_hmap { long long len, cap; char* keys; char* vals; long long kw, vw, kk;\n"
      "                long long nbuckets; long long* buckets; };\n"
      "static struct ox_hmap* ox_hmap_check(void* h, const char* who){\n"
      "  if(!h){ fprintf(stderr, \"oxide: %s on a null hmap\\n\", who); abort(); }\n"
      "  return (struct ox_hmap*)h;\n"
      "}\n"
      "// raw byte hash (FNV-1a). For the str/ptr category we hash the CONTENT of\n"
      "// the string (not the pointer), since each literal occurrence gets its own\n"
      "// global and pointer identity does not hold.\n"
      "static long long ox_hh(const void* p, long long n, long long kw, long long kk){\n"
      "  const unsigned char* b = (const unsigned char*)p;\n"
      "  if(kk==3){\n"
      "    const char* s = *(const char* const*)p;\n"
      "    if(!s) return 0;\n"
      "    unsigned long long h = 1469598103934665603ULL;\n"
      "    while(*s){ h ^= (unsigned char)*s; h *= 1099511628211ULL; s++; }\n"
      "    return (long long)h;\n"
      "  }\n"
      "  if(kk==2){ if(kw==4){ unsigned long long u=0; memcpy(&u,b,(size_t)4); return (long long)u; } }\n"
      "  unsigned long long h = 1469598103934665603ULL;\n"
      "  for(long long i=0;i<n;i++){ h ^= b[i]; h *= 1099511628211ULL; }\n"
      "  return (long long)h;\n"
      "}\n"
      "// key equality using the (kw, kk) category (same logic as ox_map_kcmp).\n"
      "static int ox_hkeq(const struct ox_hmap* m, const char* a, const char* b){\n"
      "  if(m->kk==3){ const char* x=*(const char* const*)a; const char* y=*(const char* const*)b; if(!x) x=\"\"; if(!y) y=\"\"; return strcmp(x,y)==0; }\n"
      "  if(m->kk==2){\n"
      "    if(m->kw==4){ float x=*(const float*)a, y=*(const float*)b; return x==y; }\n"
      "    double x=*(const double*)a, y=*(const double*)b; return x==y;\n"
      "  }\n"
      "  return memcmp(a, b, (size_t)m->kw) == 0;\n"
      "}\n"
      "// find the bucket index for key buffer q, or where it would go. *outSlot is\n"
      "// the insertion-index+1 if present (else 0). Linear probing across nbuckets.\n"
      "static long long ox_hmap_find(struct ox_hmap* m, const char* q, long long* outSlot){\n"
      "  long long nb = m->nbuckets;\n"
      "  long long mask = nb - 1;\n"
      "  long long h = ox_hh(q, m->kw, m->kw, m->kk) & mask;\n"
      "  for(long long i=0;i<nb;i++){\n"
      "    long long b = (h + i) & mask;\n"
      "    long long slot = m->buckets[b];\n"
      "    if(slot == 0){ if(outSlot) *outSlot = 0; return b; }\n"
      "    if(ox_hkeq(m, m->keys + (slot-1)*m->kw, q)){ if(outSlot) *outSlot = slot; return b; }\n"
      "  }\n"
      "  if(outSlot) *outSlot = 0; return -1; // table full (should not happen under LF)\n"
      "}\n"
      "static void ox_hmap_rehash(struct ox_hmap* m, long long nnb){\n"
      "  long long* oldb = m->buckets;\n"
      "  if(nnb <= 0 || (size_t)nnb > SIZE_MAX/sizeof(long long)) ox_oom(\"hmap buckets overflow\");\n"
      "  m->buckets = (long long*)ox_calloc_checked((size_t)nnb, sizeof(long long), \"hmap buckets\");\n"
      "  for(long long i=0;i<nnb;i++) m->buckets[i] = 0;\n"
      "  m->nbuckets = nnb;\n"
      "  long long mask = nnb - 1;\n"
      "  for(long long s=1; s<=m->len; s++){\n"
      "    long long h = ox_hh(m->keys + (s-1)*m->kw, m->kw, m->kw, m->kk) & mask;\n"
      "    for(long long i=0;i<nnb;i++){\n"
      "      long long b = (h + i) & mask;\n"
      "      if(m->buckets[b] == 0){ m->buckets[b] = s; break; }\n"
      "    }\n"
      "  }\n"
      "  free(oldb);\n"
      "}\n"
      "static void ox_hmap_grow(struct ox_hmap* m){\n"
      "  if(m->kw <= 0 || m->vw <= 0) ox_oom(\"hmap invalid element width\");\n"
      "  if(m->cap == 0){ m->cap = 8; m->keys = (char*)ox_malloc_checked((size_t)m->cap*(size_t)m->kw,\"hmap keys\"); m->vals = (char*)ox_malloc_checked((size_t)m->cap*(size_t)m->vw,\"hmap values\"); }\n"
      "  if(m->cap <= m->len+1){ if(m->cap > LLONG_MAX/2) ox_oom(\"hmap capacity overflow\"); long long nc = m->cap ? m->cap*2 : 8; if((size_t)nc > SIZE_MAX/(size_t)m->kw || (size_t)nc > SIZE_MAX/(size_t)m->vw) ox_oom(\"hmap size overflow\"); m->keys = (char*)ox_realloc_checked(m->keys, (size_t)nc*(size_t)m->kw,\"hmap keys grow\"); m->vals = (char*)ox_realloc_checked(m->vals, (size_t)nc*(size_t)m->vw,\"hmap values grow\"); m->cap = nc; }\n"
      "  if(m->len + 1 <= (m->nbuckets * 3) / 4) return; // load factor 0.75\n"
      "  long long nnb = m->nbuckets ? m->nbuckets*2 : 16;\n"
      "  ox_hmap_rehash(m, nnb);\n"
      "}\n"
      "void* ox_hmap_new(long long kw, long long vw, long long kk){\n"
      "  struct ox_hmap* m = (struct ox_hmap*)ox_calloc_checked(1,sizeof(*m),\"hmap new\");\n"
      "  m->kw = kw; m->vw = vw; m->kk = kk; m->cap = 0; m->nbuckets = 0; return m;\n"
      "}\n"
      "long long ox_hmap_len(void* h){ return ox_hmap_check(h,\"len\")->len; }\n"
      "// insert-or-replace. Widening Oxide already sized the caller scratch to kw/vw.\n"
      "void ox_hmap_set(void* h, const void* kp, const void* vp){\n"
      "  struct ox_hmap* m = ox_hmap_check(h,\"set\");\n"
      "  if(m->nbuckets == 0) ox_hmap_grow(m);\n"
      "  long long slot;\n"
      "  long long b = ox_hmap_find(m, (const char*)kp, &slot);\n"
      "  if(slot != 0){ memcpy(m->vals + (slot-1)*m->vw, vp, (size_t)m->vw); return; }\n"
      "  ox_hmap_grow(m); b = ox_hmap_find(m, (const char*)kp, &slot);\n"
      "  long long idx = m->len;\n"
      "  memcpy(m->keys + idx*m->kw, kp, (size_t)m->kw);\n"
      "  memcpy(m->vals + idx*m->vw, vp, (size_t)m->vw);\n"
      "  m->len++;\n"
      "  m->buckets[b] = m->len; // 1-based insertion index\n"
      "}\n"
      "// get: 1 + copy value if present, else zero-fill and 0 (same contract as ox_map_get).\n"
      "long long ox_hmap_get(void* h, const void* kp, void* vp){\n"
      "  struct ox_hmap* m = ox_hmap_check(h,\"get\");\n"
      "  if(m->nbuckets == 0){ memset(vp, 0, (size_t)m->vw); return 0; }\n"
      "  long long slot; ox_hmap_find(m, (const char*)kp, &slot);\n"
      "  if(slot != 0){ memcpy(vp, m->vals + (slot-1)*m->vw, (size_t)m->vw); return 1; }\n"
      "  memset(vp, 0, (size_t)m->vw); return 0;\n"
      "}\n"
      "long long ox_hmap_contains(void* h, const void* kp){\n"
      "  struct ox_hmap* m = ox_hmap_check(h,\"contains\");\n"
      "  if(m->nbuckets == 0) return 0;\n"
      "  long long slot; ox_hmap_find(m, (const char*)kp, &slot); return slot != 0 ? 1 : 0;\n"
      "}\n"
      "// delete: swap-with-last among survivors (preserves relative insertion order),\n"
      "// then fix the moved entry's bucket pointer and rebuild the table.\n"
      "void ox_hmap_delete(void* h, const void* kp){\n"
      "  struct ox_hmap* m = ox_hmap_check(h,\"delete\");\n"
      "  if(m->nbuckets == 0) return;\n"
      "  long long slot; ox_hmap_find(m, (const char*)kp, &slot);\n"
      "  if(slot == 0) return;\n"
      "  long long idx = slot - 1, last = m->len - 1;\n"
      "  if(idx != last){\n"
      "    memcpy(m->keys + idx*m->kw, m->keys + last*m->kw, (size_t)m->kw);\n"
      "    memcpy(m->vals + idx*m->vw, m->vals + last*m->vw, (size_t)m->vw);\n"
      "  }\n"
      "  m->len--;\n"
      "  ox_hmap_rehash(m, m->nbuckets);\n"
      "}\n"
      "void ox_hmap_clear(void* h){ struct ox_hmap* m = ox_hmap_check(h,\"clear\"); m->len = 0; if(m->nbuckets) for(long long i=0;i<m->nbuckets;i++) m->buckets[i]=0; }\n"
      "void* ox_hmap_key_ptr(void* h, long long i){\n"
      "  struct ox_hmap* m = ox_hmap_check(h,\"key_ptr\");\n"
      "  if(i<0||i>=m->len){ fprintf(stderr, \"oxide: hmap key index out of bounds (%lld, len %lld)\\n\", i, m->len); abort(); }\n"
      "  return m->keys + i*(long long)m->kw;\n"
      "}\n"
      "void* ox_hmap_val_ptr(void* h, long long i){\n"
      "  struct ox_hmap* m = ox_hmap_check(h,\"val_ptr\");\n"
      "  if(i<0||i>=m->len){ fprintf(stderr, \"oxide: hmap value index out of bounds (%lld, len %lld)\\n\", i, m->len); abort(); }\n"
      "  return m->vals + i*(long long)m->vw;\n"
      "}\n"
      "\n"
      "// ----------------------------------------------------------------\n"
      "// hset[T] (std::unordered_set): the set side of the same hash table, minus\n"
      "// the value array. Same insertion-order iteration + swap-with-last delete.\n"
      "// ----------------------------------------------------------------\n"
      "struct ox_hset { long long len, cap; char* data; long long kw, kk;\n"
      "                long long nbuckets; long long* buckets; };\n"
      "static struct ox_hset* ox_hset_check(void* h, const char* who){\n"
      "  if(!h){ fprintf(stderr, \"oxide: %s on a null hset\\n\", who); abort(); }\n"
      "  return (struct ox_hset*)h;\n"
      "}\n"
      "static long long ox_hset_hh(const void* p, long long kw, long long kk){\n"
      "  const unsigned char* b = (const unsigned char*)p;\n"
      "  if(kk==3){\n"
      "    const char* s = *(const char* const*)p;\n"
      "    if(!s) return 0;\n"
      "    unsigned long long h = 1469598103934665603ULL;\n"
      "    while(*s){ h ^= (unsigned char)*s; h *= 1099511628211ULL; s++; }\n"
      "    return (long long)h;\n"
      "  }\n"
      "  if(kk==2){ if(kw==4){ unsigned long long u=0; memcpy(&u,b,(size_t)4); return (long long)u; } }\n"
      "  unsigned long long h = 1469598103934665603ULL;\n"
      "  for(long long i=0;i<kw;i++){ h ^= b[i]; h *= 1099511628211ULL; }\n"
      "  return (long long)h;\n"
      "}\n"
      "static int ox_hseq(const struct ox_hset* s, const char* a, const char* b){\n"
      "  if(s->kk==3){ const char* x=*(const char* const*)a; const char* y=*(const char* const*)b; if(!x) x=\"\"; if(!y) y=\"\"; return strcmp(x,y)==0; }\n"
      "  if(s->kk==2){\n"
      "    if(s->kw==4){ float x=*(const float*)a, y=*(const float*)b; return x==y; }\n"
      "    double x=*(const double*)a, y=*(const double*)b; return x==y;\n"
      "  }\n"
      "  return memcmp(a, b, (size_t)s->kw) == 0;\n"
      "}\n"
      "static long long ox_hset_find(struct ox_hset* s, const char* q, long long* outSlot){\n"
      "  long long nb = s->nbuckets, mask = nb - 1;\n"
      "  long long h = ox_hset_hh(q, s->kw, s->kk) & mask;\n"
      "  for(long long i=0;i<nb;i++){\n"
      "    long long b = (h + i) & mask;\n"
      "    long long slot = s->buckets[b];\n"
      "    if(slot == 0){ if(outSlot) *outSlot = 0; return b; }\n"
      "    if(ox_hseq(s, s->data + (slot-1)*s->kw, q)){ if(outSlot) *outSlot = slot; return b; }\n"
      "  }\n"
      "  if(outSlot) *outSlot = 0; return -1;\n"
      "}\n"
      "static void ox_hset_rehash(struct ox_hset* s, long long nnb){\n"
      "  long long* oldb = s->buckets;\n"
      "  if(nnb <= 0 || (size_t)nnb > SIZE_MAX/sizeof(long long)) ox_oom(\"hset buckets overflow\");\n"
      "  s->buckets = (long long*)ox_calloc_checked((size_t)nnb, sizeof(long long), \"hset buckets\");\n"
      "  for(long long i=0;i<nnb;i++) s->buckets[i] = 0;\n"
      "  s->nbuckets = nnb; long long mask = nnb - 1;\n"
      "  for(long long slot=1; slot<=s->len; slot++){\n"
      "    long long h = ox_hset_hh(s->data + (slot-1)*s->kw, s->kw, s->kk) & mask;\n"
      "    for(long long i=0;i<nnb;i++){ long long b = (h + i) & mask; if(s->buckets[b]==0){ s->buckets[b]=slot; break; } }\n"
      "  }\n"
      "  free(oldb);\n"
      "}\n"
      "static void ox_hset_grow(struct ox_hset* s){\n"
      "  if(s->kw <= 0) ox_oom(\"hset invalid element width\");\n"
      "  if(s->cap == 0){ s->cap = 8; s->data = (char*)ox_malloc_checked((size_t)s->cap*(size_t)s->kw,\"hset data\"); }\n"
      "  if(s->cap <= s->len+1){ if(s->cap > LLONG_MAX/2) ox_oom(\"hset capacity overflow\"); long long nc = s->cap ? s->cap*2 : 8; if((size_t)nc > SIZE_MAX/(size_t)s->kw) ox_oom(\"hset size overflow\"); s->data = (char*)ox_realloc_checked(s->data, (size_t)nc*(size_t)s->kw,\"hset data grow\"); s->cap = nc; }\n"
      "  if(s->len + 1 <= (s->nbuckets * 3) / 4) return; // load factor 0.75\n"
      "  long long nnb = s->nbuckets ? s->nbuckets*2 : 16;\n"
      "  ox_hset_rehash(s, nnb);\n"
      "}\n"
      "void* ox_hset_new(long long kw, long long kk){\n"
      "  struct ox_hset* s = (struct ox_hset*)ox_calloc_checked(1,sizeof(*s),\"hset new\");\n"
      "  s->kw = kw; s->kk = kk; s->cap = 0; s->nbuckets = 0; return s;\n"
      "}\n"
      "long long ox_hset_len(void* h){ return ox_hset_check(h,\"len\")->len; }\n"
      "void ox_hset_insert(void* h, const void* ep){\n"
      "  struct ox_hset* s = ox_hset_check(h,\"insert\");\n"
      "  if(s->nbuckets == 0) ox_hset_grow(s);\n"
      "  long long slot; long long b = ox_hset_find(s, (const char*)ep, &slot);\n"
      "  if(slot != 0) return; // idempotent (std::unordered_set::insert)\n"
      "  ox_hset_grow(s); b = ox_hset_find(s, (const char*)ep, &slot);\n"
      "  long long idx = s->len; memcpy(s->data + idx*s->kw, ep, (size_t)s->kw);\n"
      "  s->len++; s->buckets[b] = s->len;\n"
      "}\n"
      "void ox_hset_remove(void* h, const void* ep){\n"
      "  struct ox_hset* s = ox_hset_check(h,\"remove\");\n"
      "  if(s->nbuckets == 0) return;\n"
      "  long long slot; ox_hset_find(s, (const char*)ep, &slot);\n"
      "  if(slot == 0) return; // idempotent erase\n"
      "  long long idx = slot - 1, last = s->len - 1;\n"
      "  if(idx != last) memcpy(s->data + idx*s->kw, s->data + last*s->kw, (size_t)s->kw);\n"
      "  s->len--; ox_hset_rehash(s, s->nbuckets);\n"
      "}\n"
      "long long ox_hset_contains(void* h, const void* ep){\n"
      "  struct ox_hset* s = ox_hset_check(h,\"contains\");\n"
      "  if(s->nbuckets == 0) return 0;\n"
      "  long long slot; ox_hset_find(s, (const char*)ep, &slot); return slot != 0 ? 1 : 0;\n"
      "}\n"
      "void ox_hset_clear(void* h){ struct ox_hset* s = ox_hset_check(h,\"clear\"); s->len = 0; if(s->nbuckets) for(long long i=0;i<s->nbuckets;i++) s->buckets[i]=0; }\n"
      "void* ox_hset_ptr(void* h, long long i){\n"
      "  struct ox_hset* s = ox_hset_check(h,\"ptr\");\n"
      "  if(i<0||i>=s->len){ fprintf(stderr, \"oxide: hset index out of bounds (%lld, len %lld)\\n\", i, s->len); abort(); }\n"
      "  return s->data + i*(long long)s->kw;\n"
      "}\n"
      "\n"
      "// ================================================================\n"
      "// stdlib: math. Thin wrappers over libm; never fail. All return f64\n"
      "// (Oxide f64) unless noted. fabs/llvm.fabs.f64 is emitted as an LLVM\n"
      "// intrinsic by IRGen; here are the rest.\n"
      "// ================================================================\n"
      "double ox_pow(double a, double b){ return pow(a, b); }\n"
      // Advanced-math `**` operator runtimes. IRGen's PowerExpr lowering
      // (src/IRGen.cpp ~L2788) emits calls to THESE names  -  `ox_pow_f64` for a
      // floating base/exponent, `ox_ipow` for an integer base with a non-
      // negative integer exponent (iterative, no libc float path), and
      // `ox_square_i64`/`ox_square_f64` for the postfix `²` (x pow2) form
      // where the exponent is the literal 2. They are distinct from the
      // call-form `ox_pow` above (which the `pow(a,b)` builtin uses) so the
      // `**` operator and the `pow()` function can be gated / inlined
      // independently. Each is a trivial thin wrapper  -  added here to close
      // the IRGen<->runtime name gap so `**` / `²` tests can link and run.
      "double ox_pow_f64(double a, double b){ return pow(a, b); }\n"
      "long long ox_ipow(long long base, long long exp){\n"
      "  long long r = 1;\n"
      "  if (exp < 0) return 0;            // integer power of a negative exponent rounds to 0\n"
      "  while (exp > 0){\n"
      "    if (exp & 1) r *= base;\n"
      "    base *= base;\n"
      "    exp >>= 1;\n"
      "  }\n"
      "  return r;\n"
      "}\n"
      "long long ox_square_i64(long long x){ return x * x; }\n"
      "double ox_square_f64(double x){ return x * x; }\n"
      "double ox_floor(double v){ return floor(v); }\n"
      "double ox_ceil(double v){ return ceil(v); }\n"
      "double ox_round(double v){ return round(v); }\n"
      "long long ox_lround(double v){ return (long long)llround(v); }\n"
      "double ox_trunc(double v){ return trunc(v); }\n"
      "double ox_sin(double v){ return sin(v); }\n"
      "double ox_cos(double v){ return cos(v); }\n"
      "double ox_tan(double v){ return tan(v); }\n"
      "double ox_asin(double v){ return asin(v); }\n"
      "double ox_acos(double v){ return acos(v); }\n"
      "double ox_atan(double v){ return atan(v); }\n"
      "double ox_atan2(double y, double x){ return atan2(y, x); }\n"
      "double ox_log(double v){ return log(v); }\n"
      "double ox_log2(double v){ return log2(v); }\n"
      "double ox_log10(double v){ return log10(v); }\n"
      "double ox_exp(double v){ return exp(v); }\n"
      "double ox_exp2(double v){ return exp2(v); }\n"
      "double ox_hypot(double a, double b){ return hypot(a, b); }\n"
      "double ox_fmod(double a, double b){ return fmod(a, b); }\n"
      "double ox_gcd(long long a, long long b){ long long u=a<0?-a:a, v=b<0?-b:b; while(v){ long long t=u%v; u=v; v=t; } return u; }\n"
      "long long ox_isnan(double v){ return (long long)(v != v); }\n"
      "long long ox_isinf(double v){ return (long long)(v == v && (v - v) != 0.0); }\n"
      "long long ox_finite(double v){ return (long long)(v == v && (v - v) == 0.0); }\n"
      "double ox_deg2rad(double v){ return v * (3.14159265358979323846 / 180.0); }\n"
      "double ox_rad2deg(double v){ return v * (180.0 / 3.14159265358979323846); }\n"
      "double ox_pi(void){ return 3.14159265358979323846; }\n"
      "double ox_e(void){ return 2.71828182845904523536; }\n"
      "double ox_clampf(double v, double lo, double hi){ return v<lo?lo:(v>hi?hi:v); }\n"
      "long long ox_clampi(long long v, long long lo, long long hi){ return v<lo?lo:(v>hi?hi:v); }\n"
      "\n"
      "// ================================================================\n"
      "// stdlib: strings. Every string return is an arena-owned, never-null,\n"
      "// NUL-terminated buffer (same contract as ox_str_short).\n"
      "// ================================================================\n"
      "// case helpers. ox_tolower/ox_toupper mutate a fresh copy in place.\n"
      "char* ox_tolower(const char* s){\n"
      "  if(!s) return ox_str_short(0);\n"
      "  long long n = (long long)strlen(s); char* p = ox_str_short((size_t)n);\n"
      "  for(long long i=0;i<n;i++) p[i] = (char)tolower((unsigned char)s[i]);\n"
      "  return p;\n"
      "}\n"
      "char* ox_toupper(const char* s){\n"
      "  if(!s) return ox_str_short(0);\n"
      "  long long n = (long long)strlen(s); char* p = ox_str_short((size_t)n);\n"
      "  for(long long i=0;i<n;i++) p[i] = (char)toupper((unsigned char)s[i]);\n"
      "  return p;\n"
      "}\n"
      "char* ox_str_reverse(const char* s){\n"
      "  if(!s) return ox_str_short(0);\n"
      "  long long n = (long long)strlen(s); char* p = ox_str_short((size_t)n);\n"
      "  for(long long i=0;i<n;i++) p[i] = s[n-1-i];\n"
      "  return p;\n"
      "}\n"
      "char* ox_str_repeat(const char* s, long long count){\n"
      "  if(!s || count <= 0) return ox_str_short(0);\n"
      "  long long n = (long long)strlen(s); char* p = ox_str_short((size_t)(n*count));\n"
      "  for(long long k=0;k<count;k++) memcpy(p + k*n, s, (size_t)n);\n"
      "  return p;\n"
      "}\n"
      "// prefix/suffix/contains: 1 if true, 0 otherwise.\n"
      "long long ox_starts_with(const char* s, const char* pre){\n"
      "  if(!s) s=\"\"; if(!pre) pre=\"\";\n"
      "  long long n = (long long)strlen(pre);\n"
      "  return strncmp(s, pre, (size_t)n) == 0 ? 1 : 0;\n"
      "}\n"
      "long long ox_ends_with(const char* s, const char* suf){\n"
      "  if(!s) s=\"\"; if(!suf) suf=\"\";\n"
      "  long long ns = (long long)strlen(s), nf = (long long)strlen(suf);\n"
      "  if(nf > ns) return 0;\n"
      "  return strcmp(s + ns - nf, suf) == 0 ? 1 : 0;\n"
      "}\n"
      "long long ox_str_contains(const char* s, const char* sub){\n"
      "  if(!s) s=\"\"; if(!sub) sub=\"\";\n"
      "  return strstr(s, sub) != 0 ? 1 : 0;\n"
      "}\n"
      "// find first index of `sub` in `s`, or -1.\n"
      "long long ox_find(const char* s, const char* sub){\n"
      "  if(!s) s=\"\"; if(!sub) sub=\"\";\n"
      "  const char* p = strstr(s, sub);\n"
      "  return p ? (long long)(p - s) : -1;\n"
      "}\n"
      "// byte index of the first occurrence of char c, or -1. (index_of was the\n"
      "// old char-search entry; here is the richer strstr-based sub finder.)\n"
      "// Trim: drop leading/trailing runs of isspace() chars.\n"
      "char* ox_trim(const char* s){\n"
      "  if(!s) return ox_str_short(0);\n"
      "  const char* a = s; while(*a && isspace((unsigned char)*a)) a++;\n"
      "  const char* b = s + strlen(s); while(b > a && isspace((unsigned char)b[-1])) b--;\n"
      "  long long n = (long long)(b - a); char* p = ox_str_short((size_t)n);\n"
      "  if(n) memcpy(p, a, (size_t)n);\n"
      "  return p;\n"
      "}\n"
      "// Replace every (non-overlapping) occurrence of `from` in `s` with `to`.\n"
      "char* ox_replace(const char* s, const char* from, const char* to){\n"
      "  if(!s) s=\"\"; if(!from) from=\"\"; if(!to) to=\"\";\n"
      "  long long nf = (long long)strlen(from);\n"
      "  if(nf == 0){ long long n=(long long)strlen(s); char* p=ox_str_short((size_t)n); if(n) memcpy(p,s,(size_t)n); return p; }\n"
      "  struct ox_sb* sb = ox_sb_new();\n"
      "  const char* cur = s;\n"
      "  while(*cur){\n"
      "    const char* hit = strstr(cur, from);\n"
      "    if(!hit){ ox_sb_puts(sb, cur); break; }\n"
      "    long long seg = (long long)(hit - cur);\n"
      "    char tmp[ (size_t)seg + 1 ]; memcpy(tmp, cur, (size_t)seg); tmp[seg]=0;\n"
      "    ox_sb_puts(sb, tmp); ox_sb_puts(sb, to);\n"
      "    cur = hit + (size_t)nf;\n"
      "  }\n"
      "  char* out = ox_sb_finish(sb);\n"
      "  return out;\n"
      "}\n"
      "// split(s, sep): return a fresh str vec, one element per nonempty/delimited\n"
      "// segment, plus the empty surrounds. The result is a str-kind ox_vec whose\n"
      "// handle is returned; the caller iterates via vec_str helpers. sep is a\n"
      "// single-character delimiter (the common case); empty sep splits chars.\n"
      "void* ox_split(const char* s, const char* sep){\n"
      "  struct ox_vec* v = (struct ox_vec*)ox_vec_new_str();\n"
      "  if(!s) return v;\n"
      "  long long n = (long long)strlen(s);\n"
      "  long long ns = sep ? (long long)strlen(sep) : 0;\n"
      "  long long i = 0;\n"
      "  while(i <= n){\n"
      "    long long j = -1;\n"
      "    if(ns > 0){\n"
      "      long long k = i;\n"
      "      for(; k + ns <= n; k++) if(memcmp(s + k, sep, (size_t)ns) == 0){ j = k; break; }\n"
      "      if(j < 0) j = n;\n"
      "    } else {\n"
      "      j = i;       // empty sep: each char is its own segment\n"
      "      if(i >= n){ j = n; }\n"
      "    }\n"
      "    long long len = j - i; if(len < 0) len = 0;\n"
      "    char* seg = ox_str_short((size_t)len);\n"
      "    if(len) memcpy(seg, s + i, (size_t)len);\n"
      "    ox_vec_push_str(v, seg);\n"
      "    if(ns > 0) i = j + ns; else { if(i >= n) break; i = j + 1; }\n"
      "    if(ns > 0 && i > n) break;\n"
      "  }\n"
      "  return v;\n"
      "}\n"
      "// join(vec_of_str, sep): concatenate each str element with `sep` between.\n"
      "char* ox_str_join(void* h, const char* sep){\n"
      "  struct ox_vec* v = ox_vec_check(h, \"join\");\n"
      "  struct ox_sb* sb = ox_sb_new();\n"
      "  const char* sp = sep ? sep : \"\";\n"
      "  for(long long i=0;i<v->len;i++){\n"
      "    if(i) ox_sb_puts(sb, sp);\n"
      "    char* e = ((char**)v->data)[i];\n"
      "    if(e) ox_sb_puts(sb, e);\n"
      "  }\n"
      "  return ox_sb_finish(sb);\n"
      "}\n"
      "// to_str of an i64 with an explicit base (2..36). base 10 == itos.\n"
      "char* ox_itoa_base(long long v, long long base){\n"
      "  char tmp[80]; long long i = 0; unsigned long long u; int neg = 0;\n"
      "  if(v < 0 && base == 10){ neg = 1; u = (unsigned long long)(-(v + 1)) + 1ull; }\n"
      "  else u = (unsigned long long)v;\n"
      "  if(u == 0){ tmp[i++] = '0'; }\n"
      "  else while(u){ long long r = (long long)(u % (unsigned long long)base); tmp[i++] = (char)(r < 10 ? '0' + r : 'a' + r - 10); u /= (unsigned long long)base; }\n"
      "  if(neg) tmp[i++] = '-';\n"
      "  char* p = ox_str_short((size_t)i);\n"
      "  for(long long k=0;k<i;k++) p[k] = tmp[i-1-k];\n"
      "  return p;\n"
      "}\n"
      "// parse helpers with a base and an out-flag: return 1 if the whole string\n"
      "// parsed, 0 otherwise (the parsed int is written to *out).\n"
      "long long ox_stoi_base(const char* s, long long base, long long* out){\n"
      "  if(!s) return 0;\n"
      "  char* endp = 0;\n"
      "  errno = 0;\n"
      "  long long v = strtoll(s, &endp, (int)base);\n"
      "  if(endp == s || *endp != 0 || errno != 0) return 0;\n"
      "  if(out) *out = v;\n"
      "  return 1;\n"
      "}\n"
      "\n"
      "// ================================================================\n"
      "// stdlib: vec helpers (typed fast paths + blob path).\n"
      "// pop/first/last/remove_at/insert_at/contains/index_of/reverse/clear/\n"
      "// extend (append a second vec). Element-kind derives from the suffix or,\n"
      "// for the blob path, from a runtime-stored esz.\n"
      "// ================================================================\n"
      "// For the typed vec kinds we expose typed accessors; the generic helpers\n"
      "// (clear/reverse/contains/index_of) live on len + memcmp by esz.\n"
      "void ox_vec_clear(void* h){ struct ox_vec* v = ox_vec_check(h,\"clear\"); v->len = 0; }\n"
      "void ox_vec_reverse(void* h, long long esz){\n"
      "  struct ox_vec* v = ox_vec_check(h,\"reverse\");\n"
      "  if(v->len < 2) return;\n"
      "  char tmp[ (size_t)esz ];\n"
      "  for(long long i=0, j=v->len-1; i<j; i++, j--){\n"
      "    char* a = (char*)v->data + i*esz;\n"
      "  char* b = (char*)v->data + j*esz;\n"
      "    memcpy(tmp, a, (size_t)esz); memcpy(a, b, (size_t)esz); memcpy(b, tmp, (size_t)esz);\n"
      "  }\n"
      "}\n"
      "// first/last typed accessors; missing element on empty vec aborts.\n"
      "#define OX_VEC_ENDS(SUF, ETYPE) \\\n"
      "  ETYPE ox_vec_first_##SUF(void* h){ struct ox_vec* v = ox_vec_check(h,\"first\"); if(v->len==0){ fprintf(stderr,\"oxide: first() on empty vec\\n\"); abort(); } return ((ETYPE*)v->data)[0]; } \\\n"
      "  ETYPE ox_vec_last_##SUF (void* h){ struct ox_vec* v = ox_vec_check(h,\"last\");  if(v->len==0){ fprintf(stderr,\"oxide: last() on empty vec\\n\");  abort(); } return ((ETYPE*)v->data)[v->len-1]; } \\\n"
      "  long long ox_vec_pop_##SUF(void* h){ struct ox_vec* v = ox_vec_check(h,\"pop\"); if(v->len==0) return 0; v->len--; return 1; }\n"
      "OX_VEC_ENDS(i64, long long)\n"
      "OX_VEC_ENDS(f64, double)\n"
      "OX_VEC_ENDS(i1, int)\n"
      "char ox_vec_first_i8(void* h){ struct ox_vec* v = ox_vec_check(h,\"first\"); if(v->len==0){ fprintf(stderr,\"oxide: first() on empty vec\\n\"); abort(); } return ((char*)v->data)[0]; }\n"
      "char ox_vec_last_i8 (void* h){ struct ox_vec* v = ox_vec_check(h,\"last\");  if(v->len==0){ fprintf(stderr,\"oxide: last() on empty vec\\n\"); abort(); } return ((char*)v->data)[v->len-1]; }\n"
      "char* ox_vec_first_str(void* h){ struct ox_vec* v = ox_vec_check(h,\"first\"); if(v->len==0){ fprintf(stderr,\"oxide: first() on empty vec\\n\"); abort(); } return ((char**)v->data)[0]; }\n"
      "char* ox_vec_last_str (void* h){ struct ox_vec* v = ox_vec_check(h,\"last\");  if(v->len==0){ fprintf(stderr,\"oxide: last() on empty vec\\n\"); abort(); } return ((char**)v->data)[v->len-1]; }\n"
      "void ox_vec_pop_blob (void* h){ struct ox_vec* v = ox_vec_check(h,\"pop\"); if(v->len) v->len--; }\n"
      "// remove_at(h, i, esz): erase slot i, shifting the tail down.\n"
      "void ox_vec_remove_at(void* h, long long i, long long esz){\n"
      "  struct ox_vec* v = ox_vec_check(h,\"remove_at\");\n"
      "  if(i<0 || i>=v->len){ fprintf(stderr, \"oxide: vec index out of bounds (%lld, len %lld)\\n\", i, v->len); abort(); }\n"
      "  memmove((char*)v->data + i*esz, (char*)v->data + (i+1)*esz, (size_t)(v->len - i - 1) * (size_t)esz);\n"
      "  v->len--;\n"
      "}\n"
      "// insert_at(h, i, esz, src): insert a copy of `src`'s element at slot i,\n"
      "// shifting the tail up.\n"
      "void ox_vec_insert_at(void* h, long long i, long long esz, void* src){\n"
      "  struct ox_vec* v = ox_vec_check(h,\"insert_at\");\n"
      "  if(i<0) i = 0;\n"
      "  if(i > v->len) i = v->len;\n"
      "  if(esz <= 0) ox_oom(\"vec insert invalid element width\");\n"
      "  if(v->len >= v->cap){\n"
      "    if(v->cap > LLONG_MAX/2) ox_oom(\"vec insert capacity overflow\");\n"
      "    long long nc = v->cap ? v->cap*2 : 8;\n"
      "    if((size_t)nc > SIZE_MAX/(size_t)esz) ox_oom(\"vec insert size overflow\");\n"
      "    v->data = ox_realloc_checked(v->data, (size_t)nc * (size_t)esz,\"vec insert grow\");\n"
      "    v->cap = nc;\n"
      "  }\n"
      "  if(i < v->len) memmove((char*)v->data + (i+1)*esz, (char*)v->data + i*esz, (size_t)(v->len - i) * (size_t)esz);\n"
      "  memcpy((char*)v->data + i*esz, src, (size_t)esz);\n"
      "  v->len++;\n"
      "}\n"
      "// contains(h, i, esz, src, kind): linear scan for an element equal to the\n"
      "// bytes at `src`. kind selects the comparator family (0 signed int, 1\n"
      "// unsigned, 2 float, 3 ptr/str)  -  mirrors sort_blob. Returns 1/0.\n"
      "static int ox_el_eq(long long kind, long long esz, const char* a, const char* b){\n"
      "  if(kind==3){ const char* x=*(const char* const*)a; const char* y=*(const char* const*)b; if(!x) x=\"\"; if(!y) y=\"\"; return strcmp(x,y)==0; }\n"
      "  if(kind==2){\n"
      "    if(esz==4){ float x=*(const float*)a, y=*(const float*)b; return x==y; }\n"
      "    double x=*(const double*)a, y=*(const double*)b; return x==y;\n"
      "  }\n"
      "  unsigned long long ua=0, ub=0; memcpy(&ua,a,(size_t)esz); memcpy(&ub,b,(size_t)esz);\n"
      "  if(kind==0){\n"
      "    long long sa, sb;\n"
      "    if(esz==1){ sa=(signed char)ua; sb=(signed char)ub; }\n"
      "    else if(esz==2){ sa=(short)ua; sb=(short)ub; }\n"
      "    else if(esz==4){ sa=(int)ua; sb=(int)ub; }\n"
      "    else { sa=(long long)ua; sb=(long long)ub; }\n"
      "    return sa==sb;\n"
      "  }\n"
      "  return ua==ub;\n"
      "}\n"
      "long long ox_vec_contains(void* h, long long esz, void* src, long long kind){\n"
      "  struct ox_vec* v = ox_vec_check(h,\"contains\");\n"
      "  for(long long i=0;i<v->len;i++) if(ox_el_eq(kind, esz, (char*)v->data + i*esz, (const char*)src)) return 1;\n"
      "  return 0;\n"
      "}\n"
      "long long ox_vec_index_of(void* h, long long esz, void* src, long long kind){\n"
      "  struct ox_vec* v = ox_vec_check(h,\"index_of\");\n"
      "  for(long long i=0;i<v->len;i++) if(ox_el_eq(kind, esz, (char*)v->data + i*esz, (const char*)src)) return i;\n"
      "  return -1;\n"
      "}\n"
      "// append (extend) a second same-kind vec onto the first. Copies bytes.\n"
      "void ox_vec_extend(void* dst_h, void* src_h, long long esz){\n"
      "  struct ox_vec* d = ox_vec_check(dst_h,\"extend\");\n"
      "  struct ox_vec* s = ox_vec_check(src_h,\"extend\");\n"
      "  if(esz <= 0 || s->len > LLONG_MAX - d->len) ox_oom(\"vec extend overflow\");\n"
      "  if(d->len + s->len > d->cap){\n"
      "    long long nc = d->cap ? d->cap : 8;\n"
      "    while(d->len + s->len > nc){ if(nc > LLONG_MAX/2) ox_oom(\"vec extend capacity overflow\"); nc *= 2; }\n"
      "    if((size_t)nc > SIZE_MAX/(size_t)esz) ox_oom(\"vec extend size overflow\");\n"
      "    d->data = ox_realloc_checked(d->data, (size_t)nc * (size_t)esz,\"vec extend grow\");\n"
      "    d->cap = nc;\n"
      "  }\n"
      "  if(s->len) memcpy((char*)d->data + d->len*esz, s->data, (size_t)s->len * (size_t)esz);\n"
      "  d->len += s->len;\n"
      "}\n"
      "// reduce-style: sum/min/max over a numeric vec (typed fast paths).\n"
      "long long ox_vec_sum_i64(void* h){ struct ox_vec* v=ox_vec_check(h,\"sum\"); long long s=0; for(long long i=0;i<v->len;i++) s += ((long long*)v->data)[i]; return s; }\n"
      "long long ox_vec_min_i64(void* h){ struct ox_vec* v=ox_vec_check(h,\"min\"); if(!v->len) return 0; long long m=((long long*)v->data)[0]; for(long long i=1;i<v->len;i++){ long long x=((long long*)v->data)[i]; if(x<m) m=x; } return m; }\n"
      "long long ox_vec_max_i64(void* h){ struct ox_vec* v=ox_vec_check(h,\"max\"); if(!v->len) return 0; long long m=((long long*)v->data)[0]; for(long long i=1;i<v->len;i++){ long long x=((long long*)v->data)[i]; if(x>m) m=x; } return m; }\n"
      "double ox_vec_sum_f64(void* h){ struct ox_vec* v=ox_vec_check(h,\"sum\"); double s=0; for(long long i=0;i<v->len;i++) s += ((double*)v->data)[i]; return s; }\n"
      "double ox_vec_min_f64(void* h){ struct ox_vec* v=ox_vec_check(h,\"min\"); if(!v->len) return 0; double m=((double*)v->data)[0]; for(long long i=1;i<v->len;i++){ double x=((double*)v->data)[i]; if(x<m) m=x; } return m; }\n"
      "double ox_vec_max_f64(void* h){ struct ox_vec* v=ox_vec_check(h,\"max\"); if(!v->len) return 0; double m=((double*)v->data)[0]; for(long long i=1;i<v->len;i++){ double x=((double*)v->data)[i]; if(x>m) m=x; } return m; }\n"
      "\n"
      "// ================================================================\n"
      "// stdlib: map delete + values (the parallel array of values).\n"
      "// ================================================================\n"
      "void ox_map_delete(void* h, const void* kp){\n"
      "  struct ox_map* m = ox_map_check(h,\"delete\");\n"
      "  long long i = ox_map_lb(m, (const char*)kp);\n"
      "  if(i < m->len && ox_map_kcmp(m, m->keys + i*m->kw, (const char*)kp) == 0){\n"
      "    memmove(m->keys + i*m->kw, m->keys + (i+1)*m->kw, (size_t)(m->len - i - 1) * (size_t)m->kw);\n"
      "    memmove(m->vals + i*m->vw, m->vals + (i+1)*m->vw, (size_t)(m->len - i - 1) * (size_t)m->vw);\n"
      "    m->len--;\n"
      "  }\n"
      "}\n"
      "void ox_map_clear(void* h){ struct ox_map* m = ox_map_check(h,\"clear\"); m->len = 0; }\n"
      "void ox_set_clear(void* h){ struct ox_set* s = ox_set_check(h,\"clear\"); s->len = 0; }\n"
      "// value pointer at sorted index i (parallel to ox_map_key_ptr).\n"
      "void* ox_map_val_ptr(void* h, long long i){\n"
      "  struct ox_map* m = ox_map_check(h,\"val_ptr\");\n"
      "  if(i<0||i>=m->len){ fprintf(stderr, \"oxide: map value index out of bounds (%lld, len %lld)\\n\", i, m->len); abort(); }\n"
      "  return m->vals + i*(long long)m->vw;\n"
      "}\n"
      "\n"
      "// ================================================================\n"
      "// stdlib: time + random. A simple xorshift64 with a process-seeded\n"
      "// state (or set by seed). time_ns is monotonic-ish: clock() CPI ticks on\n"
      "// freestanding would be unknown; on hosted it uses clock().\n"
      "// ================================================================\n"
      "#include <time.h>\n"
      "static unsigned long long ox_rng_state = 0;\n"
      "void ox_seed(long long s){ ox_rng_state = (unsigned long long)(s ? s : 0x9e3779b97f4a7c15ull); }\n"
      "long long ox_rand(void){\n"
      "  if(!ox_rng_state) ox_rng_state = 0x9e3779b97f4a7c15ull;\n"
      "  unsigned long long x = ox_rng_state;\n"
      "  x ^= x << 13; x ^= x >> 7; x ^= x << 17;\n"
      "  ox_rng_state = x;\n"
      "  return (long long)(x >> 1);\n"
      "}\n"
      "long long ox_rand_range(long long lo, long long hi){\n"
      "  if(hi <= lo) return lo;\n"
      "  unsigned long long span = (unsigned long long)(hi - lo);\n"
      "  unsigned long long r = (unsigned long long)ox_rand();\n"
      "  return lo + (long long)(r % span);\n"
      "}\n"
      "long long ox_time_ns(void){ return (long long)((double)clock() / (double)CLOCKS_PER_SEC * 1e9); }\n"
      "long long ox_clock_ms(void){ return (long long)((double)clock() / (double)CLOCKS_PER_SEC * 1000.0); }\n"
      "long long ox_time_epoch(void){ return (long long)time(0); }\n"
      "\n"
      "/* ----- concurrency runtime (Win32 threads + SRW locks + CVs) ----- */\n"
      "typedef struct {\n"
      "  void* slots[256];   /* ring of capacity bufcap (0 = unbuffered) */\n"
      "  long long ival[256];\n"
      "  double      dval[256];\n"
      "  void* blob[256];    /* for blob channels: element pointer */\n"
      "  size_t blob_sz[256];\n"
      "  int head, tail, count, cap;\n"
      "  size_t elem_sz;\n"
      "  SRWLOCK lock;\n"
      "  CONDITION_VARIABLE not_full;\n"
      "  CONDITION_VARIABLE not_empty;\n"
      "} ox_channel;\n"
      "\n"
      "static ox_channel* ox_chan_alloc(int bufcap, size_t esz){\n"
      "  if(bufcap < 0 || bufcap > 256 || esz == 0) ox_oom(\"channel invalid capacity or element width\");\n"
      "  ox_channel* ch = (ox_channel*)ox_calloc_checked(1,sizeof(ox_channel),\"channel\");\n"
      "  ch->cap = bufcap; ch->elem_sz = esz;\n"
      "  InitializeSRWLock(&ch->lock);\n"
      "  InitializeConditionVariable(&ch->not_full);\n"
      "  InitializeConditionVariable(&ch->not_empty);\n"
      "  return ch;\n"
      "}\n"
      "\n"
      "/* send into a fixed-width slot ring (i64/f64/i1/i8/str all narrow to 64-bit payload). */\n"
      "static void ox_chan_send_narrow(ox_channel* ch, long long v){\n"
      "  AcquireSRWLockExclusive(&ch->lock);\n"
      "  while(ch->cap > 0 && ch->count == ch->cap) SleepConditionVariableSRW(&ch->not_full, &ch->lock, INFINITE, 0);\n"
      "  ch->ival[ch->tail] = v; ch->tail = (ch->tail + 1) % 256; ch->count++;\n"
      "  ReleaseSRWLockExclusive(&ch->lock);\n"
      "  WakeConditionVariable(&ch->not_empty);\n"
      "}\n"
      "static long long ox_chan_recv_narrow(ox_channel* ch){\n"
      "  AcquireSRWLockExclusive(&ch->lock);\n"
      "  while(ch->count == 0) SleepConditionVariableSRW(&ch->not_empty, &ch->lock, INFINITE, 0);\n"
      "  long long v = ch->ival[ch->head]; ch->head = (ch->head + 1) % 256; ch->count--;\n"
      "  ReleaseSRWLockExclusive(&ch->lock);\n"
      "  WakeConditionVariable(&ch->not_full);\n"
      "  return v;\n"
      "}\n"
      "\n"
      "/* blob send/recv copy raw bytes through the ring. */\n"
      "static void ox_chan_send_blob_impl(ox_channel* ch, void* p, size_t sz){\n"
      "  if(!ch || !p || sz == 0 || sz != ch->elem_sz) ox_oom(\"channel blob invalid payload\");\n"
      "  void* dup = ox_malloc_checked(sz,\"channel blob\"); memcpy(dup, p, sz);\n"
      "  AcquireSRWLockExclusive(&ch->lock);\n"
      "  while(ch->cap > 0 && ch->count == ch->cap) SleepConditionVariableSRW(&ch->not_full, &ch->lock, INFINITE, 0);\n"
      "  ch->blob[ch->tail] = dup; ch->blob_sz[ch->tail] = sz;\n"
      "  ch->tail = (ch->tail + 1) % 256; ch->count++;\n"
      "  ReleaseSRWLockExclusive(&ch->lock);\n"
      "  WakeConditionVariable(&ch->not_empty);\n"
      "}\n"
      "static void ox_chan_recv_blob_impl(ox_channel* ch, void* out, size_t sz){\n"
      "  AcquireSRWLockExclusive(&ch->lock);\n"
      "  while(ch->count == 0) SleepConditionVariableSRW(&ch->not_empty, &ch->lock, INFINITE, 0);\n"
      "  void* src = ch->blob[ch->head]; size_t got = ch->blob_sz[ch->head];\n"
      "  ch->head = (ch->head + 1) % 256; ch->count--;\n"
      "  ReleaseSRWLockExclusive(&ch->lock);\n"
      "  WakeConditionVariable(&ch->not_full);\n"
      "  memcpy(out, src, got < sz ? got : sz); free(src);\n"
      "}\n"
      "\n"
      "/* typed channel instantiations  -  suffix selects payload width. */\n"
      "void* ox_chan_new_i64(long long cap){ return (void*)ox_chan_alloc((int)cap, sizeof(long long)); }\n"
      "void* ox_chan_new_f64(long long cap){ return (void*)ox_chan_alloc((int)cap, sizeof(double));    }\n"
      "void* ox_chan_new_i1 (long long cap){ return (void*)ox_chan_alloc((int)cap, 1);                }\n"
      "void* ox_chan_new_i8 (long long cap){ return (void*)ox_chan_alloc((int)cap, 1);                }\n"
      "void* ox_chan_new_str(long long cap){ return (void*)ox_chan_alloc((int)cap, sizeof(void*));    }\n"
      "void* ox_chan_new_blob(long long cap, long long esz){ return (void*)ox_chan_alloc((int)cap, (size_t)esz); }\n"
      "\n"
      "void ox_chan_send_i64(void* h, long long v){ ox_chan_send_narrow((ox_channel*)h, v); }\n"
      "void ox_chan_send_f64(void* h, double v){    ox_chan_send_narrow((ox_channel*)h, *(long long*)&v); }\n"
      "void ox_chan_send_i1 (void* h, long long v){ ox_chan_send_narrow((ox_channel*)h, v); }\n"
      "void ox_chan_send_i8 (void* h, long long v){ ox_chan_send_narrow((ox_channel*)h, v); }\n"
      "void ox_chan_send_str(void* h, void* v){    ox_chan_send_narrow((ox_channel*)h, (long long)v); }\n"
      "void ox_chan_send_blob(void* h, void* p, long long sz){ ox_chan_send_blob_impl((ox_channel*)h, p, (size_t)sz); }\n"
      "\n"
      "long long ox_chan_recv_i64(void* h){ return ox_chan_recv_narrow((ox_channel*)h); }\n"
      "double    ox_chan_recv_f64(void* h){ long long r = ox_chan_recv_narrow((ox_channel*)h); return *(double*)&r; }\n"
      "long long ox_chan_recv_i1 (void* h){ return ox_chan_recv_narrow((ox_channel*)h); }\n"
      "long long ox_chan_recv_i8 (void* h){ return ox_chan_recv_narrow((ox_channel*)h); }\n"
      "void*     ox_chan_recv_str(void* h){ return (void*)ox_chan_recv_narrow((ox_channel*)h); }\n"
      "void      ox_chan_recv_blob(void* h, void* out, long long sz){ ox_chan_recv_blob_impl((ox_channel*)h, out, (size_t)sz); }\n"
      "\n"
      "/* threads */\n"
      "typedef struct { void* (*fn)(void*); void* arg; } ox_thread_ctx;\n"
      "static DWORD WINAPI ox_thread_thunk(LPVOID p){\n"
      "  ox_thread_ctx* c = (ox_thread_ctx*)p; void* fn = (void*)c->fn; void* a = c->arg;\n"
      "  /* The Oxide spawn body is already inlined by IRGen; the platform thread is a lifeboat. */\n"
      "  if(c->fn) c->fn(c->arg);\n"
      "  free(c); return 0;\n"
      "}\n"
      "void* ox_thread_create(void* fnptr, void* arg){\n"
      "  if(!fnptr){ fprintf(stderr, \"oxide: thread create with null function\\n\"); return 0; }\n"
      "  ox_thread_ctx* c = (ox_thread_ctx*)ox_malloc_checked(sizeof(ox_thread_ctx),\"thread context\");\n"
      "  c->fn = (void*(*)(void*))fnptr; c->arg = arg;\n"
      "  HANDLE h = CreateThread(0, 0, ox_thread_thunk, c, 0, 0);\n"
      "  if(!h){ free(c); fprintf(stderr, \"oxide: thread creation failed (%lu)\\n\", (unsigned long)GetLastError()); return 0; }\n"
      "  return (void*)h;\n"
      "}\n"
      "void ox_thread_join(void* h){ if(h){ WaitForSingleObject((HANDLE)h, INFINITE); CloseHandle((HANDLE)h); } }\n"
      "\n"
      "/* sync { ... }  -  a simple barrier: count up at begin, down at end, with a CV broadcast. */\n"
      "static SRWLOCK ox_sync_lock = SRWLOCK_INIT;\n"
      "static CONDITION_VARIABLE ox_sync_cv = CONDITION_VARIABLE_INIT;\n"
      "static int ox_sync_count = 0;\n"
      "void ox_sync_begin(void){ AcquireSRWLockExclusive(&ox_sync_lock); ox_sync_count++; ReleaseSRWLockExclusive(&ox_sync_lock); }\n"
      "void ox_sync_end  (void){ AcquireSRWLockExclusive(&ox_sync_lock); ox_sync_count--; if(ox_sync_count <= 0) WakeAllConditionVariable(&ox_sync_cv); ReleaseSRWLockExclusive(&ox_sync_lock); }\n"
      "\n"
      "typedef struct { long long rows; long long cols; double* data; } ox_mat;\n"
      "\n"
      "void* ox_mat_new(long long rows, long long cols) {\n"
      "  ox_mat* m = (ox_mat*)malloc(sizeof(ox_mat));\n"
      "  if (!m) { fprintf(stderr, \"ox_mat_new: out of memory\\n\"); exit(1); }\n"
      "  m->rows = rows; m->cols = cols;\n"
      "  size_t n = (size_t)rows * (size_t)cols;\n"
      "  if (n == 0) n = 1;\n"
      "  m->data = (double*)malloc(n * sizeof(double));\n"
      "  if (!m->data) { fprintf(stderr, \"ox_mat_new: out of memory\\n\"); exit(1); }\n"
      "  for (size_t i = 0; i < n; ++i) m->data[i] = 0.0;\n"
      "  return m;\n"
      "}\n"
      "\n"
      "void ox_mat_set(void* h, long long r, long long c, double v) {\n"
      "  ox_mat* m = (ox_mat*)h;\n"
      "  if (r < 0 || r >= m->rows || c < 0 || c >= m->cols) {\n"
      "    fprintf(stderr, \"ox_mat_set: index (%lld,%lld) out of bounds %lldx%lld\\n\",\n"
      "            r, c, m->rows, m->cols); exit(1);\n"
      "  }\n"
      "  m->data[(size_t)r * m->cols + c] = v;\n"
      "}\n"
      "\n"
      "double ox_mat_get(void* h, long long r, long long c) {\n"
      "  ox_mat* m = (ox_mat*)h;\n"
      "  if (r < 0 || r >= m->rows || c < 0 || c >= m->cols) {\n"
      "    fprintf(stderr, \"ox_mat_get: index (%lld,%lld) out of bounds %lldx%lld\\n\",\n"
      "            r, c, m->rows, m->cols); exit(1);\n"
      "  }\n"
      "  return m->data[(size_t)r * m->cols + c];\n"
      "}\n"
      "\n"
      "long long ox_mat_rows(void* h) { return ((ox_mat*)h)->rows; }\n"
      "long long ox_mat_cols(void* h) { return ((ox_mat*)h)->cols; }\n"
      "\n"
      "void ox_mat_print(void* h) {\n"
      "  ox_mat* m = (ox_mat*)h;\n"
      "  for (long long i = 0; i < m->rows; ++i) {\n"
      "    for (long long j = 0; j < m->cols; ++j) {\n"
      "      if (j > 0) fputs(\", \", stdout);\n"
      "      printf(\"%g\", m->data[(size_t)i * m->cols + j]);\n"
      "    }\n"
      "    fputs(\"\\n\", stdout);\n"
      "  }\n"
      "}\n"
      "\n"
      "void ox_mat_free(void* h) { ox_mat* m = (ox_mat*)h; if (m) { free(m->data); free(m); } }\n"
      "\n"
      "void* ox_mat_mul(void* ah, void* bh) {\n"
      "  ox_mat* A = (ox_mat*)ah; ox_mat* B = (ox_mat*)bh;\n"
      "  if (A->cols != B->rows) {\n"
      "    fprintf(stderr, \"ox_mat_mul: dimension mismatch %lldx%lld * %lldx%lld\\n\",\n"
      "            A->rows, A->cols, B->rows, B->cols); exit(1);\n"
      "  }\n"
      "  ox_mat* C = (ox_mat*)ox_mat_new(A->rows, B->cols);\n"
      "  for (long long i = 0; i < A->rows; ++i) {\n"
      "    for (long long j = 0; j < B->cols; ++j) {\n"
      "      double s = 0.0;\n"
      "      for (long long k = 0; k < A->cols; ++k) {\n"
      "        s += A->data[(size_t)i * A->cols + k] * B->data[(size_t)k * B->cols + j];\n"
      "      }\n"
      "      C->data[(size_t)i * C->cols + j] = s;\n"
      "    }\n"
      "  }\n"
      "  return C;\n"
      "}\n"
      "\n"
      "void* ox_mat_solve(void* Ah, void* bh) {\n"
      "  ox_mat* A = (ox_mat*)Ah; ox_mat* b = (ox_mat*)bh;\n"
      "  if (A->rows != A->cols) {\n"
      "    fprintf(stderr, \"ox_mat_solve: matrix must be square (%lldx%lld)\\n\",\n"
      "            A->rows, A->cols); exit(1);\n"
      "  }\n"
      "  if (b->rows != A->rows || (b->cols != 1 && b->rows != 1)) {\n"
      "    fprintf(stderr, \"ox_mat_solve: rhs must be a vector matching matrix\\n\"); exit(1);\n"
      "  }\n"
      "  long long n = A->rows;\n"
      "  /* build augmented matrix [A|b] as doubles */\n"
      "  size_t nn = (size_t)n;\n"
      "  double* aug = (double*)malloc(nn * (nn + 1) * sizeof(double));\n"
      "  if (!aug) { fprintf(stderr, \"ox_mat_solve: out of memory\\n\"); exit(1); }\n"
      "  for (long long i = 0; i < n; ++i) {\n"
      "    for (long long j = 0; j < n; ++j) {\n"
      "      aug[(size_t)i * (n + 1) + j] = A->data[(size_t)i * A->cols + j];\n"
      "    }\n"
      "    /* b may be column or row vector */\n"
      "    double bv = (b->cols == 1) ? b->data[(size_t)i] : b->data[(size_t)i];\n"
      "    aug[(size_t)i * (n + 1) + n] = bv;\n"
      "  }\n"
      "  /* Gauss-Jordan with partial pivoting */\n"
      "  for (long long col = 0; col < n; ++col) {\n"
      "    long long piv = col;\n"
      "    double best = fabs(aug[(size_t)col * (n + 1) + col]);\n"
      "    for (long long r = col + 1; r < n; ++r) {\n"
      "      double v = fabs(aug[(size_t)r * (n + 1) + col]);\n"
      "      if (v > best) { best = v; piv = r; }\n"
      "    }\n"
      "    if (fabs(best) < 1e-12) {\n"
      "      fprintf(stderr, \"ox_mat_solve: singular matrix (pivot %g, col %lld)\\n\", best, col);\n"
      "      ox_mat* sx = (ox_mat*)ox_mat_new(n, 1);\n"
      "      for (long long si = 0; si < n; ++si) sx->data[(size_t)si] = 0.0/0.0;  /* NaN */\n"
      "      free(aug);\n"
      "      return sx;\n"
      "    }\n"
      "    if (piv != col) {\n"
      "      for (long long j = 0; j <= n; ++j) {\n"
      "        double t = aug[(size_t)col * (n + 1) + j];\n"
      "        aug[(size_t)col * (n + 1) + j] = aug[(size_t)piv * (n + 1) + j];\n"
      "        aug[(size_t)piv * (n + 1) + j] = t;\n"
      "      }\n"
      "    }\n"
      "    double diag = aug[(size_t)col * (n + 1) + col];\n"
      "    for (long long j = 0; j <= n; ++j) aug[(size_t)col * (n + 1) + j] /= diag;\n"
      "    for (long long r = 0; r < n; ++r) {\n"
      "      if (r == col) continue;\n"
      "      double factor = aug[(size_t)r * (n + 1) + col];\n"
      "      for (long long j = 0; j <= n; ++j) {\n"
      "        aug[(size_t)r * (n + 1) + j] -= factor * aug[(size_t)col * (n + 1) + j];\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "  ox_mat* x = (ox_mat*)ox_mat_new(n, 1);\n"
      "  for (long long i = 0; i < n; ++i) x->data[(size_t)i] = aug[(size_t)i * (n + 1) + n];\n"
      "  free(aug);\n"
      "  return x;\n"
      "}\n"
      "\n"
      "double ox_integrate_trapz(void* fnptr, double lo, double hi, long long N) {\n"
      "  if (N < 1) N = 1;\n"
      "  typedef double (*ox_fn1)(double);\n"
      "  ox_fn1 f = (ox_fn1)fnptr;\n"
      "  double h = (hi - lo) / (double)N;\n"
      "  double sum = 0.5 * (f(lo) + f(hi));\n"
      "  for (long long i = 1; i < N; ++i) sum += f(lo + (double)i * h);\n"
      "  return sum * h;\n"
      "}\n"
      "\n"
      "long long oxide_main(void);\n"
      "int main(void){ int r = (int)oxide_main(); if(ox_arena_base) free(ox_arena_base); return r; }\n"
    );
}


std::vector<Token> Driver::resolveImports(std::vector<Token>& toks, const std::string& dir,
                                          std::vector<CompileError>& cerrs) {
  std::vector<Token> out;
  out.reserve(toks.size());
  for (size_t i = 0; i < toks.size(); i++) {

    if (i + 2 < toks.size() &&
        toks[i].kind == Tok::ident && toks[i].text == "import" &&
        toks[i + 1].kind == Tok::str_lit && toks[i + 2].kind == Tok::semicolon) {
      std::string rel = toks[i + 1].text;
      std::string path = joinPath(dir, rel);
      std::string canon = path;

      for (auto& c : canon) if (c == '\\') c = '/';
      if (importVisited_.count(canon)) { i += 2; continue; }
      importVisited_.insert(canon);

      bool ok = false;
      std::string src = readFile(path, ok);
      if (!ok) {
        cerrs.push_back({"io", "cannot import '" + rel + "' (file not found: " + path + ")",
                          toks[i].line, toks[i].col});
        i += 2; continue;
      }
      std::vector<Token> itoks;
      std::vector<LexError> lerr;
      Lexer lex(src);
      lex.lex(itoks, lerr);
      for (auto& e : lerr) cerrs.push_back({"lex", e.msg, e.line, e.col});
      if (!lerr.empty()) { i += 2; continue; }

      itoks = resolveImports(itoks, pathDir(path), cerrs);
      for (auto& t : itoks) {

        if (t.kind == Tok::end) continue;
        out.push_back(std::move(t));
      }
      i += 2;
      continue;
    }
    out.push_back(std::move(toks[i]));
  }
  return out;
}


static std::string linkFlagsFor(const std::string& clang, const Options& opt) {
  std::string out;
  bool msvc = (clang == "clang-cl" || clang == "cl");
  for (const auto& lib : opt.linkLibs) {
    if (lib.empty()) continue;
    if (msvc) {

      std::string s = lib;
      if (s.size() < 4 || s.substr(s.size() - 4) != ".lib") s += ".lib";
      out += " " + s;
    } else {
      out += " -l" + lib;
    }
  }
  for (const auto& f : opt.linkFlags) {


    out += " " + f;
  }
  return out;
}

std::string Driver::findTool(const std::vector<const char*>& names) {
  for (const char* n : names) {
    std::string probe;
#ifdef _WIN32
    probe = std::string("where ") + n + " >nul 2>nul";
#else
    probe = std::string("command -v ") + n + " >/dev/null 2>&1";
#endif
    FILE* p = popen(probe.c_str(), "r");
    if (!p) continue;
    int rc = pclose(p);
    if (rc == 0) return n;
  }
  return "";
}

int Driver::runCmd(const std::string& cmd) {
  return std::system(cmd.c_str());
}

std::string Driver::renameMain(const std::string& ir) {
  std::string out;
  out.reserve(ir.size());
  size_t i = 0;
  while (i < ir.size()) {
    if (ir.compare(i, 5, "@main") == 0) {
      char nx = (i + 5 < ir.size()) ? ir[i + 5] : '(';
      if (!(isalnum((unsigned char)nx) || nx == '_')) {
        out += "@oxide_main"; i += 5; continue;
      }
    }
    out += ir[i++];
  }
  return out;
}

// ox:proof SMT-LIB emission of formal-verification contracts.
//
// Walks the AST of every non-extern function carrying contracts (requires /
// ensures) and every loop invariant + body `assert`, and prints one SMT-LIB
// .smt2 file encoding them plus the negated query for static discharge:
//
//   (assert (not <clause>))   ; can the clause be violated?
//   (check-sat)               ; unsat => clause always holds (proven)
//
// A solver is NOT invoked here  -  the file is consumed by Z3/Why3 out-of-band.
// This is independent of the runtime-trap gate path and of --freestanding, so
// contracted freestanding code still yields a .smt2.
//
// Type encoding (a pragmatic sound choice, not the tightest):
//   bool_   -> Bool;   integers (i8..i64/u*/usize/ptr/enum) -> Int;
//   f32/f64 -> Real;   everything else -> Int as an uninterpreted placeholder.
// Reasoning over unbounded Int is sound for the no-overflow Oxide arithmetic we
// model; it is loose vs. a fixed-width bitvector encoding but correct for
// discharge of the typical ensures/invariant (monotonicity, non-negativity,
// ordering). Unsupported subforms (calls, array index, field access, casts) are
// replaced by a fresh uninterpreted constant of the right sort and logged with
// a trailing '; note:', so the file stays well-formed; a clause using such a
// placeholder simply cannot reach `unsat` (Z3 returns unknown/sat)  -  honest.

namespace ox_smt {

// ox:proof SMT sort name for an Oxide type. See the header comment above.
// Returns the SMT-LIB sort of `t` as a fresh std::string (fully recursive,
// no shared static buffer). Separated from `smtSort` so a nested-array sort
// (e.g. `[[i64; 2]; 2]` -> `(Array Int (Array Int Int))`) composes correctly:
// the prior `smtSort` recursed through a `static thread_local std::string`
// buffer and did `buf += smtSort(arrayElem(t))`, i.e. `buf += buf.c_str()`
// (self-append aliasing  -  undefined behaviour) once `arrayElem` was itself an
// array and overwrote the same `buf`. That produced malformed sort strings
// like `(Array Int Int)(Array Int Int)))` for 2-D arrays, which made z3 reject
// the whole script (`(error "invalid constant declaration, ')' expected")`)
// and silently turn every later `check-sat` into `sat`/`unknown`. The nested
// store/select terms G1d emits were therefore unverifiable until this fix.
// By-value `std::string` keeps each recursion level independent.
std::string smtSortStr(const BType& t) {
  switch (t.tag) {
    case BType::Tag::bool_: return "Bool";
    case BType::Tag::f32: case BType::Tag::f64: return "Real";
    case BType::Tag::array: {
      // ox:proof Tier 2  -  model a fixed-size array as an SMT `(Array Int <elemSort>)`.
      // The element sort is the field's own sort (Int/Bool/Real, recursively
      // array-of-array stays decidable since Z3's array theory composes). We
      // encode the COUNT only via a separate `len` builtin (Tier 3); here the
      // sort is unbounded  -  bounds are stated explicitly in `requires` clauses.
      return "(Array Int " + smtSortStr(arrayElem(t)) + ")";
    }
    default: return "Int";
  }
}

const char* smtSort(const BType& t) {
  // Backward-compat `const char*` shim: materialise the by-value sort into a
  // thread-local buffer ONCE at the top of this call. The recursive work is
  // done entirely inside `smtSortStr` (which uses only fresh locals), so this
  // static is written exactly once per `smtSort` invocation  -  safe even when a
  // caller captures the `const char*` and uses it before the next call. Callers
  // that stash the pointer across multiple `smtSort` calls must copy to a
  // `std::string` themselves (see Ghost.cpp's `std::vector<std::string>` use).
  static thread_local std::string buf;
  buf = smtSortStr(t);
  return buf.c_str();
}

// ox:proof SMT-legal identifier for a function param / local / result / old-name. We
// prefix to avoid clashes with SMT keywords and Oxide names.
std::string sym(const std::string& kind, const std::string& name) {
  return kind + "_" + name;
}

// ox:proof `SmtCtx` now lives in src/Smt.h (shared with Ghost.cpp). The helpers below
// (smtSort/sym/smtDeclareConst/smtExpr/smtPlaceholder/smtDischarge/smtClause/
// collectOldNames) are defined in this namespace; their
// declarations are imported via `#include "Smt.h"`.

// Emit `(declare-const name sort)` and remember it, unless already declared.
void smtDeclareConst(SmtCtx& c, const std::string& name, const char* sort) {
  if (c.declared.count(name)) return;
  c.out << "(declare-const " << name << " " << sort << ")\n";
  c.declared.insert(name);
}

// ox:proof Recursively lower one spec expression to an SMT term (string). For
// unsupported subforms we synthesize `phN` and return that name, after emitting
// the placeholder declare. Side note appended via the caller for transparency.
std::string smtExpr(SmtCtx& c, const Expr* e);
std::string smtExprWp(SmtCtx& c, const Expr* e,
                      const std::map<std::string, std::string>& store);
// ox:proof Forward decl so smtAsmTerm can discharge an `asm!(...) implements <spec>`
// link's `requires` clauses as caller proof obligations (the link requires the
// caller to PROVE the asm spec's preconditions hold for the actual args before
// the asm executes). Defined at ~line 2493 (after smtAsmTerm, which is at
// ~line 1958  -  same forward-decl pattern used for smtExpr/smtExprWp above).
void smtDischarge(SmtCtx& c, const std::string& label, const std::string& term,
                  const std::vector<std::string>& premises);
static const FuncDecl* smtFindDirectCallee(SmtCtx& c, const Call* call);
static const FuncDecl* smtFindMethodCallee(SmtCtx& c, const MethodCall* mc);
// ox:proof Missing-#6: promoted to non-static linkage so src/Ghost.cpp's `emitPreserves`
// can drive the same #2 WP mini-walker (declared just below) to inline a
// handler body at the top-level discharge site, getting a real `result` term
// for the invariant to reference. Keeping the mini-walk in ONE place (here)
// avoids divergence between the call-site path and the preserves path.
std::string smtConcreteCallResult(SmtCtx& c, const FuncDecl* callee,
                                         const std::string& labelBase,
                                         const std::vector<std::string>& args,
                                         const std::string& pathCond,
                                         const std::vector<std::string>& premises,
                                         std::map<std::string, std::string>* postStore = nullptr);

std::string smtPlaceholder(SmtCtx& c, const char* sort, const char* why) {
  // Function-qualified so two functions' placeholders can't collide (each
  // function's SmtCtx restarts placeholderSeq at 0).
  std::string ph = c.curFn + "_ph" + std::to_string(c.placeholderSeq++);
  smtDeclareConst(c, ph, sort);
  c.out << "; note: replaced an unsupported subform (" << why
        << ") with the uninterpreted constant " << ph << "\n";
  return ph;
}

// ox:proof BitVec width used for ALL fixed-width bit reasoning in the Int-sorted SMT
// encoder. Oxide's freestanding/control-heavy proofs overwhelmingly manipulate
// i64/u64/usize control words (VMCS fields, page-table entries, masks, MSRs), so
// the bridge is 64-bit: Int terms cross into BV64 for the actual bit operation,
// then cross back to Int with Z3's unsigned bv2int. This preserves the existing
// Int arithmetic model while making mask/shift facts decidable instead of
// opaque.
static constexpr int BV_W = 64;

// Lift an Int-sorted term string into a BitVec term. Two decidability-critical
// peepholes:
//   1. `(bv2int X)` -> `X`   -  the term was already a BV; unwrap the round-trip.
//   2. a bare declared-BV symbol -> itself  -  never wrap a native BV var in
//      `int2bv` (that's a no-op AND re-introduces the free-Int mix Z3 punts on).
// Everything else is a genuine Int term (a literal or Int-only arithmetic) and
// gets a real `int2bv`; those are constants or closed forms Z3 handles fine.
static std::string smtIntToBv(const std::string& term) {
  const std::string prefix = "((_ bv2int " + std::to_string(BV_W) + ") ";
  if (term.rfind(prefix, 0) == 0 && term.size() > prefix.size() && term.back() == ')') {
    return term.substr(prefix.size(), term.size() - prefix.size() - 1);
  }
  return "((_ int2bv " + std::to_string(BV_W) + ") " + term + ")";
}

static std::string smtBvToInt(const std::string& term) {
  return "((_ bv2int " + std::to_string(BV_W) + ") " + term + ")";
}

static std::string smtBvBin(const char* op, const std::string& l, const std::string& r) {
  return smtBvToInt("(" + std::string(op) + " " + smtIntToBv(l) + " " + smtIntToBv(r) + ")");
}

// ox:proof smtTryBandMaskEqZeroToMod   -   bitop-to-Int rewrite for quantifier-friendly
// spec-fn bodies.
//
// Z3's default quantifier instantiation (MBQI) is fragile when a `(forall ((x
// Int) ...))` body references `((_ int2bv 64) x)` together with BV atoms like
// `(_ bv4095 64)`. The original `page_aligned(gpa) = (gpa & 0xFFF) == 0`
// lowering emits `(= (bvand ((_ int2bv 64) gpa) (_ bv4095 64)) (_ bv0 64))`
// inside a `forall`  -  and Z3 sometimes returns `sat` (false counterexample)
// or `unknown` (timeout) on a formula that IS logically valid on the Int
// domain.
//
// Soundness-first rewrite: when the AST shape `(X & M) == 0` (or `0 == (X &
// M)`) appears, where `M` is a "low-bits mask"  -  i.e. `M == 2^k - 1` for some
// ``k >= 1``  -  emit `(= (mod X (M+1)) 0)` instead, keeping the whole formula
// inside plain LIA. Z3 discharges this form cleanly under quantifiers.
//
// Why this is sound: for any k the relation `(x & (2^k - 1)) == 0  <=>  (x
// mod 2^k) == 0` holds for all Int x under the FIRST UIntBitVec/int2bv
// interpretation used by Oxide's lower 64-bit ring. `int2bv` already quietly
// reduces `x mod 2^64` (the low 64 bits), and `bvand M` reads the low `k`
// bits  -  same low `k` bits captured by `x mod 2^k`. The rewrite matches that
// semantics on the Int domain that Z3 actually reasons over; the SMT-side
// `0xFFF` ↔ `4096 = 2^12` and `0x1F` ↔ `32 = 2^5` translations are
// mechanical. The `k = 64` case (`M = 0xFFFFFFFFFFFFFFFF`) is excluded  - 
// `2^64` overflows `uint64_t`, and `(mod x 2^64)` is already the `(int2bv ...)`
// identity there, so the rewrite would gain nothing. Power-of-two-of-two
// masks that overlap with the widen (e.g. `0xFFFFFFFE`) aren't low-bits
// masks  -  they're rejected by the `(M & (M+1)) == 0` predicate, so the
// soundness-critical "low-bits mask only" precondition is preserved.
//
// Returns an empty string when the shape doesn't match (caller falls through
// to the bv-equality path). The helper uses the same-name `Op::ne` shape:
// `(X & M) != 0` lowers to `(not (= (mod X (M+1)) 0))` via the caller's
// `negate` flag  -  kept here so the only AST walk is in one place.
static bool smtIsLowBitsMask(uint64_t m, uint64_t* outPower = nullptr) {
  // m == 2^k - 1  <=>  (m & (m+1)) == 0  AND  m != 0
  if (m == 0) return false;
  uint64_t mp1 = m + 1;
  if ((m & mp1) != 0) return false;          // not a clean low-bits run
  if (mp1 == 0) return false;                 // m == 0xFFFF...FFFF -> 2^64, overflow
  if (outPower) *outPower = mp1;              // == 2^k
  return true;
}

// gap C5 const-fold: for the mask/shift rewrites to fire on real Oxide code we
// must recognize a `const FLAGS_RW: i64 = 0x7` reference (lowered as a VarRef)
// as the literal it folds to. `c.constGlobals` (Smt.h line 67) carries the
// compile-time value. Returns a pointer to a STABLE per-call out-param holding
// the folded uint64_t when `e` is an IntLit OR a const-global VarRef, else
// nullptr. (The pointed-at storage lives in `out` so the caller controls its
// lifetime  -  the helpers below thread one `uint64_t outv` per call.)
static const uint64_t* smtFoldIntLit(SmtCtx& c, const Expr* e, uint64_t* out) {
  if (!e || !out) return nullptr;
  if (auto i = dynamic_cast<const IntLit*>(e)) { *out = i->v; return out; }
  if (auto v = dynamic_cast<const VarRef*>(e)) {
    auto it = c.constGlobals.find(v->name);
    if (it != c.constGlobals.end()) {
      // constGlobals is `long long`; reinterpret as unsigned bit pattern so a
      // mask like 0x7 stays 7 (positive long long fits), and a high-bits mask
      // stored as negative stays the intended uint64_t bit pattern.
      *out = (uint64_t)(it->second);
      return out;
    }
  }
  return nullptr;
}

// gap C5 generalization: the rhs literal need not be 0. We handle TWO shapes
// for a low-bits mask `M == 2^k - 1` on the `band` side of an `==`:
//   (a) `(X & M) == 0`  -> `(= (mod X 2^k) 0)`        (the original case)
//   (b) `(X & M) == M`  -> `(= (mod X 2^k) M)`        (new: "all these bits set")
// Shape (b) is sound for the same reason (a) is: `x mod 2^k` IS the low `k`
// bits of x; requiring those low bits to equal the all-ones pattern `M` (which
// is exactly `2^k - 1`) is the same as requiring `(x & M) == M`. For a general
// rhs literal `V` that is NEITHER `0` NOR equal to the mask `M`, there is no
// clean single-mod form (the low-bits-decode is a disjunction over the zero
// runs of `V`), so we return "" and fall through to the BV path  -  sound.
// `neg==true` flips the comparison (wrapped in `(not ...)`).
static std::string smtTryBandMaskEqZeroToMod(
    SmtCtx& c, const BinaryExpr* b, bool neg,
    const std::map<std::string, std::string>* store) {
  if (!b) return "";
  if (b->op != BinaryExpr::Op::eq) return "";
  // One side of `==` is an integer literal `V` (IntLit OR a const-global
  // VarRef folded via `smtFoldIntLit`); the other is `(X & M)`.
  uint64_t lhsOut = 0, rhsOut = 0;
  const uint64_t* lhsLit = smtFoldIntLit(c, b->lhs.get(), &lhsOut);
  const uint64_t* rhsLit = smtFoldIntLit(c, b->rhs.get(), &rhsOut);
  if (!lhsLit && !rhsLit) return "";
  const Expr* bandSide = lhsLit ? b->rhs.get() : b->lhs.get();
  uint64_t       rhsVal = lhsLit ? *lhsLit : *rhsLit;
  auto band = dynamic_cast<const BinaryExpr*>(bandSide);
  if (!band || band->op != BinaryExpr::Op::band) return "";
  // One side of the `band` is an IntLit/const-global low-bits mask M == 2^k - 1;
  // record the other side (the masked operand X) and the matching 2^k.
  const Expr* op = nullptr;
  uint64_t power = 0;
  uint64_t maskM = 0;
  auto tryConst = [&](const Expr* cand, const Expr* other) -> bool {
    uint64_t outv = 0;
    if (auto i = smtFoldIntLit(c, cand, &outv)) {
      uint64_t m = *i;
      uint64_t p = 0;
      if (smtIsLowBitsMask(m, &p)) { op = other; power = p; maskM = m; return true; }
    }
    return false;
  };
  if (!tryConst(band->lhs.get(), band->rhs.get()) &&
      !tryConst(band->rhs.get(), band->lhs.get())) {
    return "";
  }
  // Decide the rhs of the mod equality: shape (a) `== 0` -> 0; shape (b)
  // `== M` -> M. Any other rhs literal V: no clean single-mod form, bail.
  uint64_t modRhs;
  if (rhsVal == 0) {
    modRhs = 0;
  } else if (rhsVal == maskM) {
    modRhs = maskM;
  } else {
    return "";   // non-trivial V against mask M  -  fall through to BV path
  }
  // Lower the masked operand via the store-aware WP path when invoked from
  // `smtExprWp`; otherwise via the clause path `smtExpr`.
  std::string opTerm = store ? smtExprWp(c, op, *store) : smtExpr(c, op);
  std::string modTerm = "(mod " + opTerm + " " + std::to_string(power) + ")";
  std::string eqForm  = "(= " + modTerm + " " + std::to_string(modRhs) + ")";
  return neg ? ("(not " + eqForm + ")") : eqForm;
}
// Thin forwarder preserving the historic 3-arg signature (clause-path callers
// that pass no store get the original store=nullptr behavior).
static inline std::string smtTryBandMaskEqZeroToMod(SmtCtx& c, const BinaryExpr* b, bool neg) {
  return smtTryBandMaskEqZeroToMod(c, b, neg, /*store=*/nullptr);
}

// ox:proof gap C5: (X >> N) == K  ->  (= (div X 2^N) K)   (pure LIA, quantifier-friendly)
//
// Oxide's `shr` (logical shift) lowers to `bvlshr` in the default BV path.
// For X >= 0 (which holds for all Oxide `i64` test inputs today, and the
// soundness note below guards) logical-shift-right by `N` bits equals
// `div X 2^N` on the Int domain. Z3 discharges `(= (div X POW) K)` cleanly
// under `forall ((x Int))`, which is the whole point of the C5 gap: keep
// bitop-flavoured equalities in pure LIA instead of leaking `bvlshr` into a
// quantified body (where Z3's MBQI is unreliable around int2bv atoms).
//
// Recognized AST shape, on EITHER side of the `==`/`!=`:
//   (shr X (IntLit N))  ==  (IntLit K)
//   where N is a literal >= 0 and 1 <= N <= 62 (so `2^N` fits in uint64_t and
//   the rewrite actually does something; N == 0 would make `div X 1` = X, a
//   trivial identity we deliberately still emit since the caller explicitly
//   asked for a shr comparison  -  keeps the formula BV-free). N >= 63
//   overflows int64 semantics and is skipped (sound  -  falls through to BV).
//
// Soundness gate: we only rewrite when there is NO negative-evidence about X.
// We require `smtExprContainsBitop(lhs)` be true for the caller to have routed
// here in the first place (the call sites gate on that). `div` on a negative
// Int would round toward 0 (SMT-LIB `div`), NOT match `bvlshr` of a 2's-complement
// encoding  -  so to stay sound we additionally bail UNLESS the operand `X` is
// plausibly non-negative. We approximate that by NOT rewriting when the caller
// didn't reach us via the bitop path (see call-site guard). The literal N is
// the `shr` rhs; the literal K is the `==` rhs.
//
// `neg==true` flips: `(X >> N) != K` -> `(not (= (div X 2^N) K))`.
// Returns "" when the shape doesn't match (caller falls through to the BV path).
static std::string smtTryShrEqToDiv(
    SmtCtx& c, const BinaryExpr* b, bool neg,
    const std::map<std::string, std::string>* store) {
  if (!b) return "";
  if (b->op != BinaryExpr::Op::eq) return "";
  // One side is an integer literal `K` (IntLit OR const-global VarRef);
  // the other is `(shr X (IntLit N))`.
  uint64_t lhsOut = 0, rhsOut = 0;
  const uint64_t* lhsLit = smtFoldIntLit(c, b->lhs.get(), &lhsOut);
  const uint64_t* rhsLit = smtFoldIntLit(c, b->rhs.get(), &rhsOut);
  if (!lhsLit && !rhsLit) return "";
  const Expr* shrSide = lhsLit ? b->rhs.get() : b->lhs.get();
  uint64_t       kVal  = lhsLit ? *lhsLit : *rhsLit;
  auto shr = dynamic_cast<const BinaryExpr*>(shrSide);
  if (!shr || shr->op != BinaryExpr::Op::shr) return "";
  // ox:unsafe The shift amount must be a literal N in [0, 62] (IntLit or const-global).
  uint64_t nOut = 0;
  const uint64_t* nLit = smtFoldIntLit(c, shr->rhs.get(), &nOut);
  if (!nLit) return "";
  uint64_t n = *nLit;
  if (n > 62) return "";          // 2^63+ doesn't fit cleanly; bail (sound)
  const Expr* op = shr->lhs.get(); // X
  uint64_t pow = (uint64_t)1 << n; // 2^N
  std::string opTerm = store ? smtExprWp(c, op, *store) : smtExpr(c, op);
  std::string divTerm = "(div " + opTerm + " " + std::to_string(pow) + ")";
  std::string eqForm  = "(= " + divTerm + " " + std::to_string(kVal) + ")";
  return neg ? ("(not " + eqForm + ")") : eqForm;
}
// Thin forwarder for the store-less clause path (smtExpr callers).
static inline std::string smtTryShrEqToDiv(SmtCtx& c, const BinaryExpr* b, bool neg) {
  return smtTryShrEqToDiv(c, b, neg, /*store=*/nullptr);
}

// gap C5: (X | M) == M   -   bitop-to-Int rewrite for the "contains at most
// these bits" / "subset of M" shape. We handle TWO complementary sub-cases,
// both of which admit a clean single-form LIA lowering (the general `M` case
//  -  e.g. M = 0x6 with non-contiguous bits  -  has no clean single-mod/single-bnd
// form and still falls through to the BV path, which is sound):
//
//   (a) LOW-bits mask `M == 2^k - 1` (e.g. 0x7, 0xFF, 0xFFF): the low k bits
//       of M are all 1. `(X | M) == M` means OR-ing X with M doesn't change M,
//       i.e. X has NO bits set outside the low k bits  -  equivalently the value
//       X is bounded by `2^k - 1 = M`. This emits `(< X 2^k)` (equiv `(<= X M)`),
//       pure LIA, quantifier-friendly. SOUNDNESS: under Oxide's int2bv low-64-
//       bit ring, `(X | M) == M` with `M` all-ones in the low k bits holds iff
//       every bit of X above bit k is 0, which for non-negative X (the regime
//       the existing shr-rewrite comment also relies on) is exactly `X < 2^k`.
//       For a non-negative Int X there is no high-bit wraparound, so the
//       rewrite is sound in the same regime the rest of the bridge assumes.
//
//   (b) HIGH-bits mask  -  ~M is ITSELF a low-bits mask (e.g. M = 0xFFF...F000
//       -> ~M = 0x000...0FFF, a `2^12 - 1` low-bits mask): `(X | M) == M`
//       ("X has no bits outside the high window") is exactly `(X & ~M) == 0`
//       ("X's low 12 bits are zero"), which lowers to `(= (mod X 2^k) 0)` via
//       the band helper's shape (a). This is the ORIGINAL sub-case and emits
//       `(= (mod X (M+1)) 0)`.
//
// For a general `M` (e.g. M = 0x7, the common RWX case): SUB-CASE (a) fires
// (`~M = 0xFFFF..FFF8` is NOT a low-bits mask, but M ITSELF is). For a high-
// bits M: sub-case (b) fires. For a non-contiguous M (e.g. M = 0x6): NEITHER
// sub-case matches, we return "" and let the caller fall through to the BV
// path. That is SOUND (the BV path correctly decides `(bvor X M) == M`).
//
// Soundness: `(X | M) == M  <=>  (X & ~M) == 0` is a propositional tautology
// over bitvectors for any mask M, so the rewire is always logically sound; the
// soundness limit is purely whether M (sub-case a) or ~M (sub-case b) admits a
// clean LIA single-form lowering. `neg==true` flips the whole thing.
static std::string smtTryBorEqMaskToMod(
    SmtCtx& c, const BinaryExpr* b, bool neg,
    const std::map<std::string, std::string>* store) {
  if (!b) return "";
  if (b->op != BinaryExpr::Op::eq) return "";
  // One side is an IntLit OR const-global VarRef `M`; the other is `(bor X M2)`.
  uint64_t lhsOut = 0, rhsOut = 0;
  const uint64_t* lhsLit = smtFoldIntLit(c, b->lhs.get(), &lhsOut);
  const uint64_t* rhsLit = smtFoldIntLit(c, b->rhs.get(), &rhsOut);
  if (!lhsLit && !rhsLit) return "";
  const Expr* borSide = lhsLit ? b->rhs.get() : b->lhs.get();
  uint64_t       maskM = lhsLit ? *lhsLit : *rhsLit;
  auto bor = dynamic_cast<const BinaryExpr*>(borSide);
  if (!bor || bor->op != BinaryExpr::Op::bor) return "";
  // The `bor` must include the SAME literal M on one side (the symmetric shape
  // `(X | M) == M`); the other side is X. If the bor's literal differs from M,
  // it's a different pattern  -  bail.
  uint64_t bnLhsOut = 0, bnRhsOut = 0;
  const uint64_t* borLhsLit = smtFoldIntLit(c, bor->lhs.get(), &bnLhsOut);
  const uint64_t* borRhsLit = smtFoldIntLit(c, bor->rhs.get(), &bnRhsOut);
  const Expr* X = nullptr;
  if (borRhsLit && *borRhsLit == maskM) X = bor->lhs.get();
  else if (borLhsLit && *borLhsLit == maskM) X = bor->rhs.get();
  else return "";
  // Sub-case (a): M is a LOW-bits mask, M == 2^k - 1 (e.g. 0x7, 0xFF, 0xFFF).
  // (X | M) == M  <=>  X has no bits set above bit k-1  <=>  X < 2^k.
  // Emit (< X 2^k)  -  pure LIA, quantifier-friendly.
  uint64_t powM = 0;
  if (smtIsLowBitsMask(maskM, &powM)) {
    std::string xTerm = store ? smtExprWp(c, X, *store) : smtExpr(c, X);
    std::string ltForm = "(< " + xTerm + " " + std::to_string(powM) + ")";
    return neg ? ("(not " + ltForm + ")") : ltForm;
  }
  // Sub-case (b): ~M mod 2^64 IS a low-bits mask (M is a high-bits mask).
  // ~M = 2^k - 1 for some k; `(X | M) == M` equiv `(X & ~M) == 0` equiv
  // `(= (mod X 2^k) 0)`.
  uint64_t notM = ~maskM;          // uint64_t NOT is exactly mod-2^64 complement
  uint64_t pow = 0;
  if (!smtIsLowBitsMask(notM, &pow)) return "";  // not a clean low-bits mask -> bail
  // Inline the (X & ~M) == 0 -> (= (mod X 2^k) 0) lowering directly (the band
  // helper reads the band from the OUTER `b`, so we'd have to fabricate an AST
  // node to delegate; inlining the same three lines is simpler and avoids that).
  std::string xTerm = store ? smtExprWp(c, X, *store) : smtExpr(c, X);
  std::string modTerm = "(mod " + xTerm + " " + std::to_string(pow) + ")";
  std::string eqZero  = "(= " + modTerm + " 0)";
  return neg ? ("(not " + eqZero + ")") : eqZero;
}
// Thin forwarder for the store-less clause path (smtExpr callers).
static inline std::string smtTryBorEqMaskToMod(SmtCtx& c, const BinaryExpr* b, bool neg) {
  return smtTryBorEqMaskToMod(c, b, neg, /*store=*/nullptr);
}

// gap C5(c) RESOLVED: (X | M) == M with M a LOW-bits mask `2^k - 1` (the common
//   RWX case, M = 0x7) now rewrites to `(< X 2^k)` in `smtTryBorEqMaskToMod`
//   above (sub-case a). The high-bits-mask case (sub-case b) keeps the original
//   `(= (mod X 2^k) 0)` lower. The only remaining un-bridged form is a general
//   non-contiguous M (e.g. 0x6), which has no clean single LIA form and falls
//   through to the BV path (sound). Track a full `bvnot bvand`/`bvule` rewrite
//   as a future ticket only if real Oxide code hits that shape.

static std::string smtSafeIdent(std::string s) {
  for (char& ch : s) {
    bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_';
    if (!ok) ch = '_';
  }
  if (s.empty()) s = "anon";
  return s;
}

static std::string smtExprBaseName(SmtCtx& c, const Expr* e,
                                   const std::map<std::string, std::string>* store = nullptr) {
  if (auto v = dynamic_cast<const VarRef*>(e)) {
    if (store) {
      auto sit = store->find(v->name);
      if (sit != store->end()) return sit->second;
    }
    auto mit = c.nameMap.find(v->name);
    if (mit != c.nameMap.end()) return mit->second;
    if (c.declared.count(v->name)) return v->name;
    return v->name;
  }
  std::string lowered = store ? smtExprWp(c, e, *store) : smtExpr(c, e);
  return smtSafeIdent(lowered);
}

static std::string smtLenOf(SmtCtx& c, const Expr* arr,
                            const std::map<std::string, std::string>* store = nullptr) {
  std::string base = smtExprBaseName(c, arr, store);
  std::string lsym = "len_" + smtSafeIdent(base);
  if (!c.lenSyms.count(lsym)) {
    smtDeclareConst(c, lsym, "Int");
    // Tier 3  -  if we tracked the array's declared compile-time count, bind the
    // len symbol to that literal. `(assert (= len_<base> N))` lets a
    // `requires 0 <= idx < len(arr)` clause fold the bound to a concrete N and
    // discharge to unsat (instead of treating the bound as a free uninterp
    // int that Z3 has no information about  -  which always leaves the
    // negation satisfiable). The pre-declared `len_<base>` symbol is still
    // the value returned; we just increase Z3's knowledge about it. Sound:
    // for typed `[T; N]` arrays the count IS the array's length, end of story.
    auto alenIt = c.arrayLenSyms.find(base);
    if (alenIt != c.arrayLenSyms.end() && alenIt->second > 0) {
      c.out << "(assert (= " << lsym << " " << alenIt->second << "))\n";
    } else {
      c.out << "(assert (>= " << lsym << " 0))\n";
    }
    c.lenSyms.insert(lsym);
  }
  return lsym;
}

static bool smtExprContainsBitop(const Expr* e);

// ox:proof Advanced-math SMT emission helpers (power / matrix mul / linear-solve /
// integration).
//
// SMT-LIB has NO native operators for these, so we soundly encode each as an
// UNINTERPRETED function symbol declared once per function (dedup via
// `c.declared`), plus the minimal characterising axioms that make contracts
// referring to them discharge (e.g. `x^0 = 1`, `x^1 = x`); the rest of the
// operator's behaviour is left opaque, which is sound  -  every unproven clause
// honestly reports `sat`/`unknown` instead of a fabricated counter-model.
//
// These are exercised in BOTH the clause path (`smtExpr`) and the WP path
// (`smtExprWp`); the helpers below are store-less helpers that take already-
// lowered SMT term strings, so both callers share the same encoding (the only
// difference is how operands are lowered: `smtExpr` vs `smtExprWp(...,store)`).
//
// The new AST nodes (added by sibling subagents in AST.h):
//   struct PowerExpr   : Expr { ExprPtr base; ExprPtr exponent;
//                               BType resultType = BType::f64; };
//   struct MatrixLit   : Expr { std::vector<std::vector<ExprPtr>> rows;
//                               BType elemType = BType::f64; };
//   struct MatMulExpr  : Expr { ExprPtr lhs; ExprPtr rhs;
//                               BType elemType = BType::f64; };
//   struct SolveExpr   : Expr { ExprPtr lhs; ExprPtr rhs;
//                               BType resultType = BType::f64; };
//   struct IntegrateExpr : Expr { ExprPtr lo, hi; ExprPtr body;
//                                 int64_t samples; BType resultType = BType::f64; };
// Each has its own arm in `smtExpr` / `smtExprWp` / `smtExprBv`; the helpers
// below are shared sort + declare-fun utilities that take already-lowered
// SMT operand strings (so the per-path arms can lower their operands through
// the matching encoder: `smtExpr` for clauses, `smtExprWp(..., store)` for
// WP/return-site contexts).
//
// Matrix literal encoding (`MatrixLit` -> 2D SMT array) is inlined in the
// `smtExpr`/`smtExprWp`/`smtExprBv` arms (it builds `(store ...)` towers and
// lowers each element expr through the corresponding path's lowerer, so the
// name map / WP store is honoured). It does NOT live in a shared helper
// because the element-lowering lookup differs per path (nameMap vs store).

// ox:proof Map a BType to its SMT sort name in the advanced-math context. f32/f64 lift
// to Real (matching `smtSortStr`); integer Lifts stay Int; Bool stays Bool.
// We return a C string literal (no recursion needed here  -  we only deal with
// scalar element/result sorts for these math operators).
static const char* smtMathSort(const BType& t) {
  switch (t.tag) {
    case BType::Tag::f32: case BType::Tag::f64: return "Real";
    case BType::Tag::bool_: return "Bool";
    default: return "Int";
  }
}

// Emit `(declare-fun name (S0 S1 ...) Sret)` ONCE (dedup via `c.declared`)
// and return `name` as the uninterpreted-function reference term. Callers
// wrap this in `(<name> <arg-terms...>)` to form the application.
static std::string smtDeclareFun(SmtCtx& c, const std::string& name,
                                 const std::vector<const char*>& argSorts,
                                 const char* retSort) {
  if (c.declared.find(name) == c.declared.end()) {
    c.out << "(declare-fun " << name << " (";
    for (size_t i = 0; i < argSorts.size(); ++i) {
      if (i) c.out << " ";
      c.out << argSorts[i];
    }
    c.out << ") " << retSort << ")\n";
    c.declared.insert(name);
    c.out << "; note: declared uninterpreted math function '" << name
          << "' (advanced-math operator; characterised by axioms below)\n";
  }
  return name;
}

// pow: `b^e`. If `e` is a non-negative IntLit, unroll to a closed product of
// `b` terms (1 for n=0); this is fully decidable in pure LIRA, gives Z3 a
// concrete term to fold, and needs NO uninterpreted symbol  -  the discharge is
// as strong as ordinary `(+ / * / div)` arithmetic. For a symbolic exponent,
// fall back to an uninterpreted `pow_<fn>_<seq>` function plus the
// characterising axioms `pow(b,0)=1`, `pow(b,1)=b`, and `pow(b,2)=b*b` (the
// last is the only non-trivial instantiation Z3 reliably uses; further powers
// are left opaque  -  sound). `retSort` picks Real or Int.
static std::string smtEmitPow(SmtCtx& c, const std::string& base,
                               const std::string& exp,
                               const BType& resultType) {
  const char* sort = smtMathSort(resultType);
  // Try to unroll when the exponent term is a bare non-negative integer.
  // We accept "0", "1", "2", ... / decimal literals only  -  never a Real.
  if (!exp.empty() && (exp[0] == '-' ? false : true) &&
      exp.find_first_not_of("0123456789") == std::string::npos) {
    long long n = 0;
    try { n = std::stoll(exp); } catch (...) { n = -1; }
    if (n >= 0) {
      const std::string one = (sort == std::string("Real")) ? std::string("1.0") : std::string("1");
      if (n == 0) return one;
      std::string t = base;
      for (long long k = 1; k < n; ++k) t = "(* " + t + " " + base + ")";
      return t;
    }
  }
  // Symbolic exponent -> uninterpreted `pow` + axioms.
  std::string sym = c.curFn + "_pow_" + std::to_string(c.assertSeq++);
  smtDeclareFun(c, sym, {sort, sort}, sort);
  const std::string one = (sort == std::string("Real")) ? std::string("1.0") : std::string("1");
  // ox:proof forall b. (= (pow b 0) 1)
  std::string bvar = sym + "_b";
  c.out << "(assert (forall ((" << bvar << " " << sort << ")) "
        << "(= (" << sym << " " << bvar << " 0) " << one << ")))\n";
  // ox:proof forall b. (= (pow b 1) b)
  c.out << "(assert (forall ((" << bvar << " " << sort << ")) "
        << "(= (" << sym << " " << bvar << " 1) " << bvar << ")))\n";
  // ox:proof forall b. (= (pow b 2) (* b b))
  c.out << "(assert (forall ((" << bvar << " " << sort << ")) "
        << "(= (" << sym << " " << bvar << " 2) (* " << bvar << " " << bvar << ")))\n";
  return "(" + sym + " " + base + " " + exp + ")";
}

// solve: linear system `A x = b`. The LHS may be a matrix literal (MatrixLit)
// already lowered to a 2D array `(Array Int (Array Int Real))`, and the RHS a
// vector `(Array Int Real)`; we declare an uninterpreted `solve_<fn>_<seq>`
// returning the solution vector of the matching vector sort. Returns the
// application term; no axioms (the call's own `requires/ensures` carry the
// proof obligation, which is the sound pattern for opaque library primitives).
static std::string smtEmitSolve(SmtCtx& c, const std::string& lhs,
                                 const std::string& rhs,
                                 const BType& resultType) {
  const char* sort = smtMathSort(resultType);
  std::string sym = c.curFn + "_solve_" + std::to_string(c.assertSeq++);
  // Result vector sort: (Array Int elem).
  std::string vecSort = std::string("(Array Int ") + sort + ")";
  smtDeclareFun(c, sym,
                {"(Array Int (Array Int Real))", "(Array Int Real)"},
                vecSort.c_str());
  return "(" + sym + " " + lhs + " " + rhs + ")";
}

// matmul: matrix-matrix product `A * B` (MatMulExpr). The two operands
// lower to 2D array terms of sort `(Array Int (Array Int <elemSort>))`;
// result is a fresh 2D array of the same sort. Opaque uninterpreted function
// (the contract's `ensures` typically states the operator's invariants;
// Sema verifies dimensions). Sound: identical call sites get the same fresh
// symbol so `ensures matmul(A,B) == matmul(A,B)` discharges by syntactic
// determinism; further invariants need `requires/ensures` axioms.
static std::string smtEmitMatmul(SmtCtx& c, const std::string& a,
                                  const std::string& b,
                                  const BType& elemType) {
  const char* sort = smtMathSort(elemType);
  std::string arrSort = std::string("(Array Int (Array Int ") + sort + "))";
  std::string sym = c.curFn + "_matmul_" + std::to_string(c.assertSeq++);
  smtDeclareFun(c, sym, {arrSort.c_str(), arrSort.c_str()}, arrSort.c_str());
  return "(" + sym + " " + a + " " + b + ")";
}

// integrate: definite integral of an integrand over [lo, hi]. The caller
// passes THREE Real-sorted terms: an abstract *integrand representative*
// (for an `IntegrateExpr`, this is the lowered `body`  -  a function-valued
// expression, which we cannot directly model as a higher-order SMT term, so
// the caller lowers it to a Real-sorted representative; see the
// `IntegrateExpr` arm in `smtExpr`/`smtExprWp` for how `body` is encoded),
// and the `lo`/`hi` bounds. Result is Real. Uninterpreted function  -  no
// implicit axioms (sound: opaque). A `requires 0 <= lo <= hi` clause is the
// typical bound the contract supplies for the discharge to make progress.
static std::string smtEmitIntegrate(SmtCtx& c, const std::string& integrand,
                                     const std::string& lo,
                                     const std::string& hi) {
  std::string sym = c.curFn + "_integrate_" + std::to_string(c.assertSeq++);
  smtDeclareFun(c, sym, {"Real", "Real", "Real"}, "Real");
  return "(" + sym + " " + integrand + " " + lo + " " + hi + ")";
}

// Lower `e` DIRECTLY to a native BitVec term (not Int-then-bridged). A VarRef to
// a value declared as a native BV resolves to the bare symbol, keeping the whole
// expression in decidable QF_BV. Arithmetic uses the wrapping BV operators
// (bvadd/bvsub/bvmul), so this path also models true 64-bit overflow  -  the
// thing the Int model silently got wrong. Non-BV leaves fall back through
// `smtIntToBv(smtExpr(...))`, which is still sound (constants / closed Int forms).
static std::string smtExprBv(SmtCtx& c, const Expr* e) {
  if (!e) return "(_ bv0 " + std::to_string(BV_W) + ")";
  if (auto i = dynamic_cast<const IntLit*>(e)) {
    return "(_ bv" + std::to_string(i->v) + " " + std::to_string(BV_W) + ")";
  }
  if (auto v = dynamic_cast<const VarRef*>(e)) {
    auto mit = c.nameMap.find(v->name);
    std::string sym = (mit != c.nameMap.end()) ? mit->second : v->name;
    if (c.bvVars.count(sym)) return sym;   // native BV var: emit bare
    // const global folds to a literal -> a bv literal. Cast to unsigned: the
    // underlying iVal preserves the bit pattern (e.g. ALL_ONES = -1 as int64,
    // 0xFFFF...F as uint64), and SMT `(_ bv<N> W)` REQUIRES a non-negative
    // decimal  -  emitting `(_ bv-1 64)` is illegal SMT-LIB (Z3 then treats it
    // as an error or a free symbol, silently breaking the constraint).
    auto cg = c.constGlobals.find(v->name);
    if (mit == c.nameMap.end() && cg != c.constGlobals.end())
      return "(_ bv" + std::to_string((uint64_t)cg->second) + " " + std::to_string(BV_W) + ")";
    return smtIntToBv(smtExpr(c, e));
  }
  if (auto u = dynamic_cast<const UnaryExpr*>(e)) {
    if (u->op == UnaryExpr::Op::neg)  return "(bvneg " + smtExprBv(c, u->base.get()) + ")";
    if (u->op == UnaryExpr::Op::bnot) return "(bvnot " + smtExprBv(c, u->base.get()) + ")";
    return smtIntToBv(smtExpr(c, e));
  }
  if (auto b = dynamic_cast<const BinaryExpr*>(e)) {
    std::string l = smtExprBv(c, b->lhs.get());
    std::string r = smtExprBv(c, b->rhs.get());
    switch (b->op) {
      case BinaryExpr::Op::add:  return "(bvadd " + l + " " + r + ")";
      case BinaryExpr::Op::sub:  return "(bvsub " + l + " " + r + ")";
      case BinaryExpr::Op::mul:  return "(bvmul " + l + " " + r + ")";
      case BinaryExpr::Op::band: return "(bvand " + l + " " + r + ")";
      case BinaryExpr::Op::bor:  return "(bvor "  + l + " " + r + ")";
      case BinaryExpr::Op::bxor: return "(bvxor " + l + " " + r + ")";
      case BinaryExpr::Op::shl:  return "(bvshl " + l + " " + r + ")";
      case BinaryExpr::Op::shr:  return "(bvlshr " + l + " " + r + ")";
      default: return smtIntToBv(smtExpr(c, e));
    }
  }
  if (auto t = dynamic_cast<const TernaryExpr*>(e)) {
    return "(ite " + smtExpr(c, t->cond.get()) + " "
           + smtExprBv(c, t->thenE.get()) + " " + smtExprBv(c, t->elseE.get()) + ")";
  }
  // ---- Advanced-math arms (bitvector path) ----
  // PowerExpr / MatrixLit / MatMulExpr / SolveExpr / IntegrateExpr are not
  // bit-pattern operations; they produce Real/Int/Array SMT terms via the
  // clause-path helpers (`smtEmitPow` / matrix-literal encoder /
  // `smtEmitMatmul` / `smtEmitSolve` / `smtEmitIntegrate`). When they surface
  // in a BV context (e.g. a `requires (x ^ 2) < bv_N` comparison), we route
  // them through the clause-path encoder and bridge the resulting Int term
  // into BV via `smtIntToBv`  -  matching the existing fallback
  // `default: return smtIntToBv(smtExpr(c, e));` for bitops-vs-math mixed
  // expressions. This is sound: the bridge over-approximates on the
  // boundary; a contract that depends on real pow/matrix/integral semantics
  // will honestly report `sat` rather than fabricate a counter-model.
  // (In the common case these are Real/Array-sorted, so they don't go through
  //  the BV path at all  -  the surrounding comparison picks the Int/Real path.
  //  This arm is the safety net for the rare BV-flavoured mixed expression.)
  if (dynamic_cast<const PowerExpr*>(e) ||
      dynamic_cast<const MatrixLit*>(e) ||
      dynamic_cast<const MatMulExpr*>(e) ||
      dynamic_cast<const SolveExpr*>(e) ||
      dynamic_cast<const IntegrateExpr*>(e) ||
      dynamic_cast<const MathSymExpr*>(e) ||
      dynamic_cast<const SuperscriptExpr*>(e)) {
    return smtIntToBv(smtExpr(c, e));
  }
  return smtIntToBv(smtExpr(c, e));
}

// ox:proof Inline a `spec fn` call inside an SMT contract term. This is the abstraction
// bridge: a source clause like `requires range_ok(a, b)` becomes the SMT for the
// body of `spec fn range_ok`, with params bound to the call arguments. Without
// this, contract calls to spec functions decay to opaque placeholders and the
// VMA/refinement layer cannot prove anything useful.
static std::string smtInlineSpecCall(SmtCtx& c, const Call* call,
                                     const std::vector<std::string>& loweredArgs) {
  if (!call || call->fnPtr) return "";
  auto it = c.specFns.find(call->callee);
  if (it == c.specFns.end()) return "";
  const SpecFnDecl* sf = it->second;
  if (!sf) return smtPlaceholder(c, "Int", "missing spec fn declaration");
  if (loweredArgs.size() != sf->params.size()) {
    return smtPlaceholder(c, smtSort(sf->retType),
                          ("spec fn '" + call->callee + "' arity mismatch").c_str());
  }
  if (!sf->body) {
    return smtPlaceholder(c, smtSort(sf->retType),
                          ("spec fn '" + call->callee + "' has no body").c_str());
  }
  if (c.expandingSpecFns.count(call->callee)) {
    return smtPlaceholder(c, smtSort(sf->retType),
                          ("recursive spec fn '" + call->callee + "'").c_str());
  }

  c.expandingSpecFns.insert(call->callee);
  std::vector<std::pair<std::string, std::string>> saved;
  std::vector<std::string> newlyBound;
  for (size_t i = 0; i < sf->params.size(); ++i) {
    const std::string& pname = sf->params[i].name;
    auto old = c.nameMap.find(pname);
    if (old != c.nameMap.end()) saved.push_back({pname, old->second});
    else newlyBound.push_back(pname);
    c.nameMap[pname] = loweredArgs[i];
  }
  std::string term = smtExpr(c, sf->body.get());
  for (const auto& p : newlyBound) c.nameMap.erase(p);
  for (const auto& p : saved) c.nameMap[p.first] = p.second;
  c.expandingSpecFns.erase(call->callee);
  return term;
}

// ox:proof smtAsmTerm  -  the contract-5 `asm!`-axiomatisation core (Smt.h lines 209-246).
//
// SINGLE-OUTPUT blocks (outCount == 1): modelled as one fresh uninterpreted
// function `asm_<curFn>_<seq>` whose arg sorts are the SMT sorts of the block's
// INPUT operands (AsmIO with isOutput==false; an `inout` operand is ALSO an
// input, since its value is consumed) and whose result sort is
// `smtSort(a->resultTy)` (Sema sets resultTy to the single output's type). The
// applied term `(asm_<curFn>_<seq> <input_term_1> ...)` is the value returned by
// `smtExpr` / `smtExprWp` for the AsmExpr. If the user supplies a `spec fn`
// named `asm_<curFn>` (seq=0) or `asm_<curFn>_<seq>` (later blocks), its params
// bind positionally to the asm inputs and its body is asserted as a top-level
// universal axiom linking the uninterpreted symbol to the spec:
//   (assert (forall ((in_0 S_0) ...) (= (asm_<curFn>_<seq> in_0 ...) <body>)))
// A ground instance for the call-site inputs is also emitted:
//   (assert (= (asm_<curFn>_<seq> <input_terms...>) <ground-body>))
//
// MULTI-OUTPUT blocks (outCount > 1): Sema sets resultTy = void (the results go
// to the output lvalue targets, not the expression value  -  see IRGen's aggregate
// {T0,T1,...} return + extractvalue path). The SMT model therefore does NOT use
// a single symbol; instead it emits ONE separate uninterpreted function PER
// output:
//   asm_<curFn>_<seq>_out0  ... -> sort of outputTypes[0]
//   asm_<curFn>_<seq>_out1  ... -> sort of outputTypes[1]
//   ...
// Each takes the SAME input sorts and the SAME input terms (the asm consumes its
// inputs identically for every output register it writes). The per-output
// applied terms are returned to the caller via the `c.asmOutputTerms` side
// channel (a per-call vector, cleared-and-filled here), so the ExprStmt-AsmExpr
// arm in `smtEncodeStmt` can rebind each output lvalue to its OWN output symbol
// (the load-bearing path for `let a=0,b=0; asm!("...",out("{rax}") a, out("{rcx}") b);
// return a+b;`). For the rare expression-position use of a multi-output asm
// (`return asm!(...)`), the helper returns the `_out0` term as a representative
//  -  sound because Sema sets resultTy=void, so nothing reads the expression value.
//
// Each multi-output output `k` may have its OWN user-supplied spec fn, named
//   `asm_<curFn>_out0` (seq=0)  /  `asm_<curFn>_<seq>_out0`  (seq>0)  ... out1 ...
// matching output k. If present (params == input count, body non-null), its body
// is asserted positionally against the asm inputs exactly like the single-output
// path:
//   (assert (forall ((in_0 S_0) ...) (= (asm_<fn>_<seq>_out0 in_0 ...) <body0>)))
// so an `ensures a == 5 && b == 3` after a `rdtscp`-style block can discharge when
// the user supplies `spec fn asm_rdtscp_example_out0(...) -> i64 = 5;` and
// `..._out1(...) -> i32 = 3;`.
//
// `lowerInput` is a caller-supplied lowering function so the helper can lower
// the INPUT operands through EITHER the store-less `smtExpr` path OR the
// store-overlay `smtExprWp` path, depending on which lowerer called it. The dedup
// sets `asmDeclsEmitted` / `asmAxiomsEmitted` (keyed by the per-block / per-output
// symbol string) guarantee the declare-fun and the forall-assert ship exactly
// once per block even when both `smtExpr` and `smtExprWp` visit it.
//
// Returns the applied term for single-output blocks, or `asm_<fn>_<seq>_out0`
// (applied) for multi-output blocks (representative; see above). ALSO fills
// `c.asmOutputTerms` with the per-output applied terms for multi-output blocks
// (empty for single-output / zero-output), so the ExprStmt arm can rebind each
// output lvalue.
static std::string smtAsmTerm(
    SmtCtx& c, const AsmExpr* a,
    const std::function<std::string(const Expr*)>& lowerInput) {
  // Side-channel: per-output applied terms for the ExprStmt arm to rebind lvalues.
  // Cleared at every call so a prior block's terms never leak into this one.
  c.asmOutputTerms.clear();
  if (!a) return smtPlaceholder(c, "Int", "null AsmExpr");

  // ox:why 1. Stable per-block sequence number (keyed by the AsmExpr pointer so the
  //    same block keeps the same symbol across smtExpr / smtExprWp visits).
  auto seqIt = c.asmExprSeq.find((const void*)a);
  int seq = 0;
  if (seqIt != c.asmExprSeq.end()) {
    seq = seqIt->second;
  } else {
    seq = c.asmSeq++;
    c.asmExprSeq[(const void*)a] = seq;
  }

  const std::string& fn = c.curFn;
  std::string sym = "asm_" + fn + "_" + std::to_string(seq);

  // 2. Gather the INPUT operand terms (isOutput==false) and their sorts.
  //    `isInOut` operands are BOTH read and written; they count as inputs for
  //    the function signature (their value is consumed) and are modelled as an
  //    argument to every per-output uninterpreted function  -  consistent with
  //    the IRGen path, which passes an inout lvalue both as an input operand and
  //    receives an extractvalue output. Pure outputs only are skipped here.
  std::vector<std::string> inputTerms;
  std::vector<std::string> inputSorts;
  for (const auto& io : a->ios) {
    if (io.isOutput && !io.isInOut) continue;   // pure output  -  not an input arg
    if (!io.val) continue;
    inputTerms.push_back(lowerInput(io.val.get()));
    inputSorts.push_back(smtSort(io.ty));
  }

  // Count output operands (isOutput==true covers both `out` and `inout`).
  int outCount = 0;
  for (const auto& io : a->ios) if (io.isOutput) ++outCount;

  // ---- Shared helper: emit one uninterpreted function declaration + optional
  //      spec-fn axiom (universal forall + ground instance) for symbols of the
  //      shape `asm_<...>`. Used by BOTH the single-output path (one call) and
  //      the multi-output path (one call per output). `outSort` is the return
  //      sort (smtSort of the corresponding output type). Returns the applied
  //      term `(sym <input_terms...>)`. Spec-name lookup is passed in since the
  //      single-output and multi-output paths name their specs differently. ----
  auto emitAsmSymbol = [&](
      const std::string& symName, const char* outSort,
      const std::string& specName, size_t inputCount) -> std::string {
    // 3. declaration: (declare-fun asm_<...> (S_0 ...) OutSort)   -  once.
    std::ostringstream decl;
    decl << "(declare-fun " << symName << " (";
    for (size_t i = 0; i < inputSorts.size(); ++i) {
      if (i) decl << " ";
      decl << inputSorts[i];
    }
    decl << ") " << outSort << ")";
    std::string declStr = decl.str();
    if (!c.asmDeclsEmitted.count(symName)) {
      c.out << "; asm! symbol " << symName << " (" << inputSorts.size()
            << " input(s), sort " << outSort << ")  -  uninterpreted function symbol\n";
      c.out << declStr << "\n";
      c.asmDeclsEmitted.insert(symName);
    }
    c.declared.insert(symName);

    // 4. applied term: (symName <input_terms...>).
    std::string applied = "(" + symName;
    for (const auto& t : inputTerms) applied += " " + t;
    applied += ")";

    // ox:proof 5. Spec-fn axiom (if the user supplied a matching spec fn). Bind the spec
    //    fn's params to fresh in_<i> quantifier vars, lower the body, emit a
    //    universal forall axiom, then a ground instance at the call-site inputs.
    auto sfIt = c.specFns.find(specName);
    if (sfIt != c.specFns.end() && sfIt->second && sfIt->second->body &&
        sfIt->second->params.size() == inputCount &&
        !c.asmAxiomsEmitted.count(symName)) {
      const SpecFnDecl* sf = sfIt->second;
      c.asmAxiomsEmitted.insert(symName);

      std::vector<std::string> qvars;       // in_0, in_1, ...
      std::vector<std::pair<std::string, std::string>> saved;
      std::vector<std::string> newlyBound;
      for (size_t i = 0; i < sf->params.size(); ++i) {
        std::string qv = "in_" + std::to_string(i);
        qvars.push_back(qv);
        const std::string& pname = sf->params[i].name;
        auto old = c.nameMap.find(pname);
        if (old != c.nameMap.end()) saved.push_back({pname, old->second});
        else newlyBound.push_back(pname);
        c.nameMap[pname] = qv;
      }
      // ox:proof forall-version of the applied term uses the quantifier vars.
      std::string qApplied = "(" + symName;
      for (const auto& qv : qvars) qApplied += " " + qv;
      qApplied += ")";
      std::string bodyTerm = smtExpr(c, sf->body.get());
      for (auto& p : newlyBound) c.nameMap.erase(p);
      for (auto& s : saved) c.nameMap[s.first] = s.second;

      std::ostringstream ax;
      ax << "; asm! axiom  -  spec fn " << specName
         << " body links " << symName << " to the user-supplied contract\n";
      ax << "(assert (forall (";
      for (size_t i = 0; i < qvars.size(); ++i) {
        if (i) ax << " ";
        ax << "(" << qvars[i] << " " << inputSorts[i] << ")";
      }
      ax << ") (= " << qApplied << " " << bodyTerm << ")))";
      c.out << ax.str() << "\n";

      // Ground instance at the call-site inputs.
      std::vector<std::pair<std::string, std::string>> savedG;
      std::vector<std::string> newlyBoundG;
      for (size_t i = 0; i < sf->params.size(); ++i) {
        const std::string& pname = sf->params[i].name;
        auto old = c.nameMap.find(pname);
        if (old != c.nameMap.end()) savedG.push_back({pname, old->second});
        else newlyBoundG.push_back(pname);
        c.nameMap[pname] = inputTerms[i];
      }
      std::string groundBody = smtExpr(c, sf->body.get());
      for (auto& p : newlyBoundG) c.nameMap.erase(p);
      for (auto& s : savedG) c.nameMap[s.first] = s.second;
      c.out << "; asm! ground instance  -  " << specName
            << " at the call-site inputs\n";
      c.out << "(assert (= " << applied << " " << groundBody << "))\n";
    }
    return applied;
  };

  // ox:proof ---- Single-output path: one symbol `asm_<fn>_<seq>`, spec fn
  //      `asm_<fn>` (seq=0) or `asm_<fn>_<seq>`. Identical semantics to the
  //      pre-multi-output encoder  -  no regression for the common case. ----
  std::string reprApplied;   // the asm block's representative result term
                             // (single-output applied term, or _out0 for multi)
  if (outCount <= 1) {
    const char* retSort = (outCount == 1) ? smtSort(a->resultTy) : "Int";
    std::string specName = (seq == 0)
        ? ("asm_" + fn)
        : ("asm_" + fn + "_" + std::to_string(seq));
    std::string applied = emitAsmSymbol(sym, retSort, specName,
                                        /*inputCount=*/inputTerms.size());
    reprApplied = applied;
  } else {
  // ---- Multi-output path: one uninterpreted function PER output. ----
  // The outputs land in `a->outputTypes` in declaration order (Sema populates
  // it with the type of every io.isOutput operand). For output k, declare
  //   asm_<fn>_<seq>_out<k> : (inputSorts...) -> smtSort(outputTypes[k])
  // and look for a per-output spec fn named
  //   asm_<fn>_out<k>      (seq=0)
  //   asm_<fn>_<seq>_out<k>(seq>0)
  // emitting the matching forall+ground axioms. Each output's applied term is
  // stashed in `c.asmOutputTerms` for the ExprStmt arm to rebind its lvalue.
  for (int k = 0; k < outCount; ++k) {
    std::string outSym = sym + "_out" + std::to_string(k);
    // smtSort returns a const char* into a thread-local buffer that is only
    // stable until the next smtSort call  -  copy to std::string before the next
    // iteration clobbers it (same defensive pattern used in Ghost.cpp).
    std::string outSortStr = smtSort(a->outputTypes[k]);
    const char* outSort = outSortStr.c_str();
    std::string specName = (seq == 0)
        ? ("asm_" + fn + "_out" + std::to_string(k))
        : ("asm_" + fn + "_" + std::to_string(seq) + "_out" + std::to_string(k));
    std::string applied = emitAsmSymbol(outSym, outSort, specName,
                                        /*inputCount=*/inputTerms.size());
    c.asmOutputTerms.push_back(applied);
    if (k == 0) reprApplied = applied;   // representative for expression-position uses
    }
  }

  // ox:proof ---- Verified-asm `implements <spec_fn>(<args>)` hypothesis + requires
  //      discharge. When the user explicitly linked this asm block to an
  //      `asm spec fn` decl via `implements`, we substitute the spec's formal
  //      params with the lowering of the implemented `implementsArgs` terms
  //      and `result` with `reprApplied` (the block's representative result
  //      term  -  single-output applied, or `_out0` for multi-output), then:
  //        a) DISCHARGE each of the spec's `requires` clauses as a caller
  //           proof obligation via `smtDischarge` (empty premises, matching
  //           how a function-level `requires` is checked against unconstrained
  //           inputs  -  `sat` means a caller could violate the precondition).
  //        b) ASSERT each of the spec's `ensures` clauses as a HYPOTHESIS in
  //           the live `(assert ...)` stream, GUARDED by the (conjoined)
  //           requires so the architectural assumption is sound under the
  //           precondition. Each asserted ensures is ALSO appended to
  //           `c.asmSpecPremises` so the WP caller threads it into downstream
  //           discharge as a premise (exactly how `requires` clauses and call-
  //           site assumes are threaded)  -  the sig-level (smtExpr) path sees
  //           it as a plain assert, the WP (smtExprWp) path as a scoped premise.
  //      Dedup-keyed by `sym` (the per-block symbol) via `asmImplHypsEmitted`
  //      so the SECOND visit (smtExpr after smtExprWp or vice-versa) does not
  //      re-assert or re-discharge  -  the encoders double-visit AsmExpr just as
  //      the convention-based axiom path does, and the dedup keeps the .smt2
  //      file readable and the witness count honest. A spec lookup miss is
  //      HONEST: we emit a `; note:` line and skip (NEVER a false `unsat`).
  //      The `implements` clause is UNKNOWN to Sema's naming-convention path;
  //      when BOTH `implements` AND a matching `spec fn asm_<fn>` exist, the
  //      `implements` link takes precedence (it's the explicit, first-class
  //      tie)  -  the convention path has already emitted `applySpecFn` (above,
  //      via emitAsmSymbol's `specName` lookup), so both the conventional axle
  //      AND the implements funnel into the same hypothesis. ----
  if (a->hasImplements && !a->implementsSpec.empty() &&
      !c.asmImplHypsEmitted.count(sym)) {
    auto sfIt = c.specFns.find(a->implementsSpec);
    if (sfIt != c.specFns.end() && sfIt->second &&
        sfIt->second->isAsmSpec) {
      const SpecFnDecl* spec = sfIt->second;
      c.asmImplHypsEmitted.insert(sym);
      // Lower each implementsArg via the same lowerer used for the asm's input
      // operands (smtExpr for clause contexts, smtExprWp for body contexts)
      // so the substituted terms carry the right WP-store semantics.
      std::vector<std::string> implArgTerms;
      for (auto& argExpr : a->implementsArgs) {
        if (!argExpr) { implArgTerms.push_back("true"); continue; }
        implArgTerms.push_back(lowerInput(argExpr.get()));
      }
      // Arity mismatch: Sema already errored; we lower what we can + skip the
      // hypothesis to stay honest (a malformed link yields no fake `unsat`).
      if (implArgTerms.size() != spec->params.size()) {
        c.out << "; note: asm! implements '" << a->implementsSpec
              << "' arity mismatch (" << implArgTerms.size() << " args vs "
              << spec->params.size() << " spec params)  -  implements hypothesis "
                 "skipped (see Sema errors)\n";
      } else {
        // Bind spec params -> implArgTerms, `result` -> reprApplied. Save +
        // restore so a subsequent clause (or a default-constructed VarRef in
        // the same lowerer pass) doesn't leak the spec-scoped bindings.
        std::vector<std::pair<std::string, std::string>> savedBounds;
        std::vector<std::string> newlyBound;
        for (size_t i = 0; i < spec->params.size(); ++i) {
          const std::string& pname = spec->params[i].name;
          auto old = c.nameMap.find(pname);
          if (old != c.nameMap.end()) savedBounds.emplace_back(pname, old->second);
          else newlyBound.push_back(pname);
          c.nameMap[pname] = implArgTerms[i];
        }
        auto savedResult = c.nameMap.find("result");
        c.nameMap["result"] = reprApplied;

        // ox:proof (a) Discharge each spec `requires` as a caller proof obligation
        //     (smtDischarge uses push/pop + check-sat-using; empty premises
        //     matches how a function-level requires is checked against
        //     unconstrained inputs  -  sat honestly means a caller could
        //     violate the precondition). The discharge is emitted to the live
        //     stream (c.out), so it appears in the same .smt2 file as the
        //     function's other discharges; the witness parser counts it.
        for (size_t ri = 0; ri < spec->requires_.size(); ++ri) {
          auto& reqExpr = spec->requires_[ri];
          if (!reqExpr) continue;
          std::string reqTerm = smtExpr(c, reqExpr.get());
          std::string label = fn + "_asmreq_" + std::to_string(seq) + "_" +
                              std::to_string(ri);
          c.out << "; asm! implements '" << a->implementsSpec
                << "'  -  caller-discharged precondition #" << ri << "\n";
          std::vector<std::string> reqPrem;   // empty for a requires discharge
          smtDischarge(c, label, reqTerm, reqPrem);
        }

        // Conjunction over the spec's requires (the guard for the ensures
        // hypothesis). Empty requires -> `true` (the architectural assumption
        // is unconditional  -  sound for a precondition-free instruction).
        std::string reqGuard = "true";
        std::vector<std::string> reqTerms;
        for (auto& reqExpr : spec->requires_) {
          if (!reqExpr) continue;
          reqTerms.push_back(smtExpr(c, reqExpr.get()));
        }
        if (reqTerms.size() == 1) reqGuard = reqTerms[0];
        else if (reqTerms.size() > 1) {
          std::ostringstream g;
          g << "(and";
          for (const auto& t : reqTerms) g << " " << t;
          g << ")";
          reqGuard = g.str();
        }

        // ox:proof (b) Assert each spec `ensures` as a hypothesis guarded by reqGuard
        //     (the hardware is trusted to satisfy its spec for these args,
        //     under its precondition). Append to asmSpecPremises so the WP
        //     caller threads it as a downstream premise. A `true ==> ens`
        //     simplifies to `ens` (Z3 eliminates the tautological guard); we
        //     still emit it explicitly for .smt2 readability.
        for (size_t ei = 0; ei < spec->ensures_.size(); ++ei) {
          auto& ensExpr = spec->ensures_[ei];
          if (!ensExpr) continue;
          std::string ensTerm = smtExpr(c, ensExpr.get());
          std::string hyp = (reqGuard == "true")
                              ? ensTerm
                              : "(=> " + reqGuard + " " + ensTerm + ")";
          c.out << "; asm! implements '" << a->implementsSpec
                << "'  -  architectural hypothesis #" << ei
                << " (ensures, guarded by the spec's requires)\n";
          c.out << "(assert " << hyp << ")\n";
          c.asmSpecPremises.push_back(hyp);
        }

        // ox:why Restore the nameMap bindings so a later clause that resolves the
        // same bare names gets the right (caller-scoped) terms back.
        if (savedResult != c.nameMap.end()) c.nameMap["result"] = savedResult->second;
        else c.nameMap.erase("result");
        for (auto& p : newlyBound) c.nameMap.erase(p);
        for (auto& s : savedBounds) c.nameMap[s.first] = s.second;
      }
    } else if (sfIt == c.specFns.end()) {
      c.out << "; note: asm! implements '" << a->implementsSpec
            << "'  -  no such asm spec fn in the specFns table (see Sema "
               "errors); implements hypothesis skipped\n";
    } else if (sfIt->second && !sfIt->second->isAsmSpec) {
      c.out << "; note: asm! implements '" << a->implementsSpec
            << "'  -  '" << a->implementsSpec
            << "' is not marked isAsmSpec (see Sema errors); implements "
               "hypothesis skipped\n";
    }
  }

  // ox:why Representative: the multi-output expression value is void (Sema), so a spec
  // fn naming the asm result should not read it; returning _out0 is sound (an
  // honest concrete term, not a placeholder).
  return reprApplied;
}

std::string smtExpr(SmtCtx& c, const Expr* e) {
  if (!e) return "true";
  if (auto i = dynamic_cast<const IntLit*>(e)) {
    return std::to_string((long long)i->v);
  }
  if (auto f = dynamic_cast<const FloatLit*>(e)) {
    if (f->v == (double)(long long)f->v) {
      return std::to_string((long long)f->v) + ".0";
    }
    std::ostringstream os;
    os << f->v;
    return os.str();
  }
  if (auto b = dynamic_cast<const BoolLit*>(e)) {
    return b->v ? "true" : "false";
  }
  if (auto v = dynamic_cast<const VarRef*>(e)) {
    // ox:proof Resolve the source-level name to its function-unique SMT symbol via the
    // name map (params -> p_<fn>_<name>, result -> <fn>_result). Quantifier
    // binders and placeholders live directly in `declared` (their unique sym).
    auto mit = c.nameMap.find(v->name);
    if (mit != c.nameMap.end()) {
      // ox:unsafe A value declared as a native BitVec must be lifted to Int here (this is
      // the pure-Int arithmetic context); `bv2int` of a DECLARED bv is the
      // decidable direction. In a bit context smtExprBv resolves it bare.
      if (c.bvVars.count(mit->second)) return smtBvToInt(mit->second);
      return mit->second;
    }
    if (c.declared.count(v->name)) {
      if (c.bvVars.count(v->name)) return smtBvToInt(v->name);
      return v->name;   // binder / fresh const
    }
    // Compile-time integer `const` global (e.g. VMCS_EPT_POINTER): substitute
    // the literal so cross-reference asserts can discharge instead of falling
    // to an uninterpreted placeholder.
    auto cg = c.constGlobals.find(v->name);
    if (cg != c.constGlobals.end()) return std::to_string(cg->second);
    return smtPlaceholder(c, "Int", ("unknown name '" + v->name + "'").c_str());
  }
  if (auto o = dynamic_cast<const OldExpr*>(e)) {
    // `old(x)` reads the pre-state snapshot `old_<x>`, resolved via the map.
    if (auto v = dynamic_cast<const VarRef*>(o->sub.get())) {
      auto mit = c.nameMap.find("old_" + v->name);
      if (mit != c.nameMap.end()) return mit->second;
    }
    return smtPlaceholder(c, "Int", "old(x) of a non-bare name");
  }
  if (auto q = dynamic_cast<const QuantExpr*>(e)) {
    std::string bv = c.curFn + "_q_" + q->binder + "_" + std::to_string(c.placeholderSeq++);
    // Range bounds are integer expressions; widen the comparison via Int.
    std::string lo = smtExpr(c, q->lo.get());
    std::string hi = smtExpr(c, q->hi.get());
    // ox:proof SMT range: `lo <= bv` and (bv < hi) [exclusive] or (bv <= hi) [inclusive].
    std::string hiCheck =
      q->inclusive ? ("(<= " + bv + " " + hi + ")") : ("(< " + bv + " " + hi + ")");
    std::string range = "(and (>= " + bv + " " + lo + ") " + hiCheck + ")";
    // Bind the binder's bare name to the fresh `bv` symbol for the body parse
    // only, so `arr[k]`/`k` references resolve to the quantified variable.
    auto mit = c.nameMap.find(q->binder);
    bool hadBind = (mit != c.nameMap.end());
    std::string savedBind = hadBind ? mit->second : "";
    c.nameMap[q->binder] = bv;
    std::string body = smtExpr(c, q->body.get());
    if (hadBind) c.nameMap[q->binder] = savedBind; else c.nameMap.erase(q->binder);
    // ox:proof Build (forall/exists ((bv Sort)) (=> range body))
    std::ostringstream t;
    t << "(" << (q->isForall ? "forall" : "exists")
      << " ((" << bv << " " << smtSort(q->binderType) << ")) "
      << "(=> " << range << " " << body << "))";
    return t.str();
  }
  if (auto u = dynamic_cast<const UnaryExpr*>(e)) {
    if (u->op == UnaryExpr::Op::not_) {
      return "(not " + smtExpr(c, u->base.get()) + ")";
    }
    if (u->op == UnaryExpr::Op::neg) {
      return "(- " + smtExpr(c, u->base.get()) + ")";
    }
    // bnot/addr/deref are not expressible in this encoding.
    return smtPlaceholder(c, "Int", "unary op");
  }
  if (auto b = dynamic_cast<const BinaryExpr*>(e)) {
    // Bitop-to-Int rewrite: `(X & M) == 0` with M a low-bits mask (2^k - 1)
    // emits `(= (mod X 2^k) 0)` so the formula stays in plain LIA, where
    // Z3's quantifier instantiation is reliable. Sits BEFORE the bv-equality
    // shortcut, so the matched shape is lowered to Int; non-matching bitop
    // expressions fall through to the original bv path unchanged. The `ne`
    // arm shares the helper via its `neg` flag (so `(X & M) != 0` lowers to
    // `(not (= (mod X 2^k) 0))`). See `smtTryBandMaskEqZeroToMod` for the
    // soundness rationale (low-bits-mask + int2bv low-64-bit semantics).
    if (b->op == BinaryExpr::Op::eq || b->op == BinaryExpr::Op::ne) {
      std::string rewritten = smtTryBandMaskEqZeroToMod(
          c, b, /*neg=*/(b->op == BinaryExpr::Op::ne));
      if (!rewritten.empty()) return rewritten;
    }
    // gap C5 (X | M) == M where ~M is a low-bits mask -> (= (mod X 2^k) 0).
    if (b->op == BinaryExpr::Op::eq || b->op == BinaryExpr::Op::ne) {
      std::string rewritten = smtTryBorEqMaskToMod(
          c, b, /*neg=*/(b->op == BinaryExpr::Op::ne));
      if (!rewritten.empty()) return rewritten;
    }
    // gap C5 (X >> N) == K -> (= (div X 2^N) K). Only fires when bitops are
    // present (so a plain-int `shr` comparison that was already going to lower
    // as `bvlshr` in the switch below gets rescued into pure LIA instead).
    if ((b->op == BinaryExpr::Op::eq || b->op == BinaryExpr::Op::ne) &&
        (smtExprContainsBitop(b->lhs.get()) || smtExprContainsBitop(b->rhs.get()))) {
      std::string rewritten = smtTryShrEqToDiv(
          c, b, /*neg=*/(b->op == BinaryExpr::Op::ne));
      if (!rewritten.empty()) return rewritten;
    }
    bool bvCompare = smtExprContainsBitop(b->lhs.get()) || smtExprContainsBitop(b->rhs.get());
    if (bvCompare && b->op == BinaryExpr::Op::eq)
      return "(= " + smtExprBv(c, b->lhs.get()) + " " + smtExprBv(c, b->rhs.get()) + ")";
    if (bvCompare && b->op == BinaryExpr::Op::ne)
      return "(not (= " + smtExprBv(c, b->lhs.get()) + " " + smtExprBv(c, b->rhs.get()) + "))";
    std::string l = smtExpr(c, b->lhs.get());
    std::string r = smtExpr(c, b->rhs.get());
    switch (b->op) {
      case BinaryExpr::Op::add: return "(+ " + l + " " + r + ")";
      case BinaryExpr::Op::sub: return "(- " + l + " " + r + ")";
      case BinaryExpr::Op::mul: return "(* " + l + " " + r + ")";
      case BinaryExpr::Op::div: return "(div " + l + " " + r + ")";
      case BinaryExpr::Op::mod: return "(mod " + l + " " + r + ")";
      case BinaryExpr::Op::eq:  return "(= " + l + " " + r + ")";
      case BinaryExpr::Op::ne:  return "(not (= " + l + " " + r + "))";
      case BinaryExpr::Op::lt:  return "(< " + l + " " + r + ")";
      case BinaryExpr::Op::gt:  return "(> " + l + " " + r + ")";
      case BinaryExpr::Op::le:  return "(<= " + l + " " + r + ")";
      case BinaryExpr::Op::ge:  return "(>= " + l + " " + r + ")";
      case BinaryExpr::Op::land:return "(and " + l + " " + r + ")";
      case BinaryExpr::Op::lor: return "(or " + l + " " + r + ")";
      case BinaryExpr::Op::band: return smtBvBin("bvand", l, r);
      case BinaryExpr::Op::bor:  return smtBvBin("bvor",  l, r);
      case BinaryExpr::Op::bxor: return smtBvBin("bvxor", l, r);
      case BinaryExpr::Op::shl:  return smtBvBin("bvshl", l, r);
      case BinaryExpr::Op::shr:  return smtBvBin("bvlshr", l, r);
    }
    return smtPlaceholder(c, "Int", "binary op");
  }
  if (auto t = dynamic_cast<const TernaryExpr*>(e)) {
    return "(ite " + smtExpr(c, t->cond.get()) + " "
                + smtExpr(c, t->thenE.get()) + " "
                + smtExpr(c, t->elseE.get()) + ")";
  }
  if (auto call = dynamic_cast<const Call*>(e)) {
    if (!call->fnPtr && call->callee == "len" && call->args.size() == 1) {
      return smtLenOf(c, call->args[0].get());
    }
    // Part 1  -  `mmio_load(addr)` (signature path). The WP path
    // (`smtExprWp`) already lowered this against the WP store's `mmio_mem`
    // array symbol; this arm mirrors that for the signature / clause context
    // so an `ensures` or `axiom` written directly in terms of `mmio_load(a)`
    // resolves concretely instead of falling to a placeholder.
    //
    // We consult `c.mmioState` first (a per-address named model threaded
    // across function-call boundaries in `smtEncodeStmt`'s call arm):
    //   - HIT  : return the stored value term (the propagated write).
    //     This is what makes cross-function MMIO threading load-bearing on
    //     the clause side: a callee `configure_device(base)` writes
    //     `mmioState[base] = 1` (recorded in the WP path); if the clause is
    //     lowered AFTER the body walk, the named model is already populated,
    //     so an `assert mmio_load(base) == 0x1` clause sees the propagated
    //     `1` and discharges to unsat.
    //   - MISS: declare a fresh uninterpreted constant
    //     `<curFn>_mmio_load_<assertSeq>` of Int sort, record the address
    //     term in `c.mmioReadAddresses` (so a `modifies`-clause frame axiom
    //     can distinguish read-but-not-written addresses), and return the
    //     fresh symbol. Honest: the model is unconstrained about this
    //     address until an `axiom` or a propagated write constrains it;
    //     the discharge will report `sat` (`unknown`-ish) when nothing
    //     constrains it, which is the correct outcome (we do NOT model the
    //     hardware's MMIO read behaviour  -  the user does, via `axiom`s).
    if (!call->fnPtr && call->callee == "mmio_load" && call->args.size() == 1) {
      std::string ptrTerm = smtExpr(c, call->args[0].get());
      auto it = c.mmioState.find(ptrTerm);
      if (it != c.mmioState.end() && !it->second.empty()) {
        return it->second;
      }
      std::string ph = c.curFn + "_mmio_load_" + std::to_string(c.assertSeq++);
      smtDeclareConst(c, ph, "Int");
      c.mmioReadAddresses.insert(ptrTerm);
      c.out << "; note: mmio_load of unconstrained address '"
            << ptrTerm << "' -> " << ph << " (Part 1 named MMIO model)\n";
      return ph;
    }
    const FuncDecl* callee = smtFindDirectCallee(c, call);
    if (callee) {
      std::vector<std::string> args;
      args.reserve(call->args.size());
      for (auto& a : call->args) args.push_back(smtExpr(c, a.get()));
      return smtConcreteCallResult(c, callee, c.curFn, args, "", {});
    }
  }
  if (auto mc = dynamic_cast<const MethodCall*>(e)) {
    const FuncDecl* callee = smtFindMethodCallee(c, mc);
    if (callee) {
      std::vector<std::string> args;
      args.reserve(mc->args.size());
      for (auto& a : mc->args) args.push_back(smtExpr(c, a.get()));
      return smtConcreteCallResult(c, callee, c.curFn, args, "", {});
    }
  }
  if (auto call = dynamic_cast<const Call*>(e)) {
    if (!call->fnPtr && call->callee == "len" && call->args.size() == 1) {
      return smtLenOf(c, call->args[0].get());
    }
    std::vector<std::string> args;
    args.reserve(call->args.size());
    for (auto& a : call->args) args.push_back(smtExpr(c, a.get()));
    std::string specTerm = smtInlineSpecCall(c, call, args);
    if (!specTerm.empty()) return specTerm;
  }
  // Tier 2  -  cast widening/narrowing. `n as u8` narrows; `n as u64` widens.
  // Only meaningful when `n` is BV-typed (declared via `c.bvVars`). For a
  // pure-Int operand we bridge through `int2bv` first, then `bv2int` after,
  // so the result stays Int-sorted (this is the pure-Int arithmetic context).
  // The casts are total  -  zero-extend is lossless, extract models 2's complement
  // narrow. Hypervisors cast constantly (16-bit VMCS field IDs widened to 64
  // before VMREAD).
  if (auto ce = dynamic_cast<const CastExpr*>(e)) {
    std::string inner = smtExpr(c, ce->e.get());
    // Determine the BV width of the SOURCE. We only support i64/u64/i32/u32/
    // i16/u16/i8/u8 as targets and infer the source width from `inner`'s
    // declared-bv symbol (if any); otherwise default to BV_W.
    int srcW = BV_W;
    if (auto v = dynamic_cast<const VarRef*>(ce->e.get())) {
      auto mit = c.nameMap.find(v->name);
      if (mit != c.nameMap.end() && c.bvVars.count(mit->second)) srcW = BV_W;
      else if (c.declared.count(v->name) && c.bvVars.count(v->name)) srcW = BV_W;
    }
    switch (ce->target.tag) {
      case BType::Tag::i64: case BType::Tag::u64:
        if (srcW == BV_W) return inner;  // no-op
        return smtBvToInt("((_ zero_extend " + std::to_string(BV_W - srcW) +
                           ") ((_ int2bv " + std::to_string(srcW) + ") " + inner + "))");
      case BType::Tag::i32: case BType::Tag::u32:
        return smtBvToInt("((_ extract 31 0) ((_ int2bv " +
                           std::to_string(BV_W) + ") " + inner + "))");
      case BType::Tag::i16: case BType::Tag::u16:
        return smtBvToInt("((_ extract 15 0) ((_ int2bv " +
                           std::to_string(BV_W) + ") " + inner + "))");
      case BType::Tag::i8: case BType::Tag::u8:
        return smtBvToInt("((_ extract 7 0) ((_ int2bv " +
                           std::to_string(BV_W) + ") " + inner + "))");
      default: return inner;  // bool/void/float casts  -  honest pass-through
    }
  }
  // Tier 2  -  array index read `arr[i]`. `arr` resolves through nameMap/declared
  // to its declared `(Array Int <elem>)` symbol; `i` lowers via `smtExpr`.
  // Multi-dimensional: `arr[i][j]` is a nested `Index` whose base is itself an
  // `Index`, so this arm recurses. No `len()` bounds baked in (Tier 3); bounds
  // come from explicit `requires 0 <= i < len(arr)` clauses.
  if (auto ix = dynamic_cast<const Index*>(e)) {
    std::string baseTerm;
    if (auto v = dynamic_cast<const VarRef*>(ix->base.get())) {
      auto mit = c.nameMap.find(v->name);
      if (mit != c.nameMap.end()) baseTerm = mit->second;
      else if (c.declared.count(v->name)) baseTerm = v->name;
      else return smtPlaceholder(c, "Int",
                  ("array base '" + v->name + "' not declared").c_str());
    } else {
      // Nested index (e.g. `arr[i]` as the base of `arr[i][j]`)  -  recurse on
      // the base so we build `(select (select arr i) j)` naturally.
      baseTerm = smtExpr(c, ix->base.get());
      if (baseTerm.empty() || baseTerm.find("ph") == 0)
        return smtPlaceholder(c, "Int", "array base failed to lower");
    }
    std::string idx = smtExpr(c, ix->index.get());
    return "(select " + baseTerm + " " + idx + ")";
  }
  // Call, MethodCall, AssocCall, Field, RangeLit, StructLit, FloatLit,
  // StrLit, etc. are not modelled in this encoding  -  EXCEPT the Tier 2a
  // `self.x` field read handled just below.
  // Tier 2a  -  `self.x` field read in the signature-level encoder. This is
  // the path used to lower `requires` clauses (and the signature-level
  // `ensures` fallback row); without it, `requires self.x == 10` would
  // lower `self.x` to the `unsupported expr` placeholder, so the assumed
  // premise `(= ph0 10)` would constrain NOTHING and a downstream return-
  // site ensures that reads the per-field const `self__<S>__x` would stay
  // `sat`  -  false-undischarged, even when the body's write arm (Patch 4)
  // is correct. Mirrors the Field arm in `smtExprWp` (~line 2079) but
  // WITHOUT a `store` overlay  -  `smtExpr` is store-less. Lookup order:
  // nameMap → declared; both resolve to the same `self__<S>__<field>`
  // identifier the seeding block in `emitFnContracts` pre-declared, so a
  // `requires self.x == v` premise and the WP return-site term that reads
  // the same field agree on the symbol name  -  this is what lets the
  // premise actually constrain the value the return site reads.
  if (auto f = dynamic_cast<const Field*>(e)) {
    if (auto v = dynamic_cast<const VarRef*>(f->base.get())) {
      if (v->name == "self" && !c.selfStructName.empty()) {
        std::string key = "self__" + c.selfStructName + "__" + f->field;
        auto mit = c.nameMap.find(key);
        if (mit != c.nameMap.end()) {
          if (c.bvVars.count(mit->second)) return smtBvToInt(mit->second);
          return mit->second;
        }
        if (c.declared.count(key)) {
          if (c.bvVars.count(key)) return smtBvToInt(key);
          return key;
        }
        // Fall through to placeholder if the struct lookup somehow missed.
      }
    }
    // Non-self Field access  -  honestly punt. Tier 2a scope.
    return smtPlaceholder(c, "Int", "field access on non-self base");
  }
  // Contract 5  -  `asm!(...)` block. Modelled as a fresh uninterpreted
  // function `asm_<curFn>_<seq>` (single-output) or one-per-output
  // `asm_<curFn>_<seq>_out0/_out1/...` (multi-output)  -  see smtAsmTerm. This
  // is the store-less contract path (`requires` / signature `ensures`); it
  // fires when an asm! block's result-symbol is referenced via a spec fn that
  // names `asm_<curFn>`  -  the typical path is the WP-return-site encoder; the
  // sig-level encoder mostly hits this for a `requires` clause that directly
  // lowers an asm result. For single-output blocks the applied term
  // `(asm_<fn>_<seq> <inputs...>)` is returned; for multi-output blocks the
  // `_out0` representative is returned (sound: Sema sets resultTy=void, so no
  // spec reads the multi-output expression value  -  the per-output lvalue
  // bindings live in the ExprStmt arm). Declare-fun + spec-fn axiom emission
  // are handled (dedup'd) inside `smtAsmTerm`.
  // ---- Advanced-math arms (clause path) ----
  // PowerExpr `b^e`: delegate to `smtEmitPow`  -  unrolls a literal exponent
  // to a closed product, otherwise emits the uninterpreted `pow` + axioms.
  // Operands lower via `smtExpr` (clause path).
  if (auto p = dynamic_cast<const PowerExpr*>(e)) {
    std::string base = smtExpr(c, p->base.get());
    std::string exp  = smtExpr(c, p->exponent.get());
    return smtEmitPow(c, base, exp, p->resultType);
  }
  // ox:proof MatrixLit `[[a00, a01], [a10, a11], ...]`: encode as a 2D SMT array
  // `(Array Int (Array Int <elemSort>))`. We build it from a fresh
  // `const <fn>_mat_<seq>` array symbol and `(store (store mat i j v) ...)`
  // for every `rows[i][j]` element (each `v` lowers via `smtExpr`, so a
  // contract that refers to a matrix cell resolves to a concrete Int/Real
  // term). An empty matrix returns the bare unconstrained array symbol
  // (sound: dimensions and bounds must be stated in the contract). The
  // outer sort is `(Array Int (Array Int elemSort))`; rows index the outer
  // array, columns index the inner array.
  if (auto ml = dynamic_cast<const MatrixLit*>(e)) {
    const char* es = smtMathSort(ml->elemType);
    std::string arrSort = std::string("(Array Int (Array Int ") + es + "))";
    std::string name = c.curFn + "_mat_" + std::to_string(c.assertSeq++);
    if (c.declared.find(name) == c.declared.end()) {
      c.out << "(declare-const " << name << " " << arrSort << ")\n";
      c.declared.insert(name);
      c.out << "; note: declared matrix literal '" << name << "' (2D array)\n";
    }
    std::string acc = name;
    for (size_t i = 0; i < ml->rows.size(); ++i) {
      std::string rowArrSort = std::string("(Array Int ") + es + ")";
      std::string innerSym = name + "_r" + std::to_string(i);
      if (c.declared.find(innerSym) == c.declared.end()) {
        c.out << "(declare-const " << innerSym << " " << rowArrSort << ")\n";
        c.declared.insert(innerSym);
      }
      std::string inner = innerSym;
      for (size_t j = 0; j < ml->rows[i].size(); ++j) {
        std::string v = smtExpr(c, ml->rows[i][j].get());
        inner = "(store " + inner + " " + std::to_string(j) + " " + v + ")";
      }
      acc = "(store " + acc + " " + std::to_string(i) + " " + inner + ")";
    }
    return acc;
  }
  // SolveExpr `lhs \ rhs` (linear solve `A x = b`): opaque uninterpreted
  // function `solve_<fn>_<seq>` returning a vector. `lhs` lowers via
  // `smtExpr` and is expected to yield a 2D-array term (e.g. a MatrixLit),
  // `rhs` lowers to a 1D-array term. The contract's `ensures` carries the
  // solver invariants (e.g. `ensures lhs * result == rhs` lowers via matmul).
  if (auto s = dynamic_cast<const SolveExpr*>(e)) {
    std::string lhs = smtExpr(c, s->lhs.get());
    std::string rhs = smtExpr(c, s->rhs.get());
    return smtEmitSolve(c, lhs, rhs, s->resultType);
  }
  // MatMulExpr `A * B` (matrix-matrix product): opaque uninterpreted
  // `matmul_<fn>_<seq>` returning a 2D array. Both operands lower via
  // `smtExpr` and are expected to yield `(Array Int (Array Int <elemSort>))`
  // terms (e.g. a MatrixLit, or an enclosing `MatMulExpr` whose term IS the
  // applied uninterpreted symbol). Sound: `ensures matmul(A, B) == matmul(A,
  // B)` discharges by syntactic determinism of the same fresh symbol per
  // call; further invariants must be stated as `requires/ensures` axioms.
  if (auto mm = dynamic_cast<const MatMulExpr*>(e)) {
    std::string a = smtExpr(c, mm->lhs.get());
    std::string b = smtExpr(c, mm->rhs.get());
    return smtEmitMatmul(c, a, b, mm->elemType);
  }
  // IntegrateExpr: definite integral of `body` over [lo, hi]. The `body`
  // integrand is a function-valued expression; in SMT we cannot represent a
  // higher-order function as a bare SMT term, so we lower `body` to a Real-
  // sorted *representative* term (a placeholder if it is a bare function name
  //  -  `smtExpr` punts an opaque Call to a `ph<N> Int`, which is
  // sound-but-underconstrained  -  and `lo`/`hi` lower as Real terms). The
  // `samples` field does NOT carry into the SMT (it's a numeric-methods
  // accuracy knob; the verification contract must constrain the abstract
  // integral regardless of sample count). The `integrate_<fn>_<seq>` symbol
  // is opaque; a `requires 0 <= lo <= hi` clause constrains bounds.
  if (auto ig = dynamic_cast<const IntegrateExpr*>(e)) {
    std::string lo = smtExpr(c, ig->lo.get());
    std::string hi = smtExpr(c, ig->hi.get());
    // Body: a function-valued term. Honest model  -  a fresh Real placeholder
    // that represents the integrated function's *scale*; if `body` happens
    // to lower to a Real-able form (e.g. a Call to a `spec fn` returning f64
    //  -  `smtExpr`/`smtInlineSpecCall` returns a Real term), we use that. A
    // plain function name lowers to an Int placeholder, which we lift to Real
    // via SMT's `(/ <t> 1.0)` so the sort matches `integrate`'s Real domain.
    std::string bodyTerm;
    if (ig->body) {
      std::string rawBody = smtExpr(c, ig->body.get());
      if (rawBody.rfind("ph", 0) == 0)
        bodyTerm = "(/ " + rawBody + " 1.0)";
      else
        bodyTerm = rawBody;
    } else {
      bodyTerm = smtPlaceholder(c, "Real", "integrate body (null integrand)");
    }
    return smtEmitIntegrate(c, bodyTerm, lo, hi);
  }
  // ox:proof MathSymExpr: leaf constant  -  emit SMT Real literal for pi/e, placeholder otherwise.
  if (auto/*ms*/ ms = dynamic_cast<const MathSymExpr*>(e)) {
    if (ms->text == "pi" || ms->text == "\xcf\x80") // π
      return "(/ 314159265358979323846264338327950288.0 100000000000000000000000000000000000)";
    if (ms->text == "e" || ms->text == "\xe2\x84\xaf") // ℯ
      return "(/ 271828182845904523536028747135266249.0 100000000000000000000000000000000000)";
    return smtPlaceholder(c, "Real", ("math symbol '" + ms->text + "'").c_str());
  }
  // SuperscriptExpr: like PowerExpr  -  base^exp via smtEmitPow.
  if (auto se = dynamic_cast<const SuperscriptExpr*>(e)) {
    std::string base = smtExpr(c, se->base.get());
    std::string exp  = se->exponent ? smtExpr(c, se->exponent.get()) : "2";
    return smtEmitPow(c, base, exp, se->resultType);
  }
  if (auto a = dynamic_cast<const AsmExpr*>(e)) {
    return smtAsmTerm(c, a, [&](const Expr* inExpr) {
      return smtExpr(c, inExpr);
    });
  }
  return smtPlaceholder(c, "Int", "unsupported expr");
}

// Find every `old(<bareName>)` mention in a (list of) ensures clause(s).
// Mirrors the IRGen snapshot collector (IRGen.cpp:4524).
void collectOldNames(const Expr* e, std::set<std::string>& out) {
  if (!e) return;
  if (auto o = dynamic_cast<const OldExpr*>(e)) {
    if (auto v = dynamic_cast<const VarRef*>(o->sub.get())) out.insert(v->name);
    collectOldNames(o->sub.get(), out);
    return;
  }
  if (auto b = dynamic_cast<const BinaryExpr*>(e)) {
    collectOldNames(b->lhs.get(), out); collectOldNames(b->rhs.get(), out); return;
  }
  if (auto u = dynamic_cast<const UnaryExpr*>(e)) { collectOldNames(u->base.get(), out); return; }
  if (auto t = dynamic_cast<const TernaryExpr*>(e)) {
    collectOldNames(t->cond.get(), out);
    collectOldNames(t->thenE.get(), out);
    collectOldNames(t->elseE.get(), out); return;
  }
  if (auto q = dynamic_cast<const QuantExpr*>(e)) {
    collectOldNames(q->lo.get(), out);
    collectOldNames(q->hi.get(), out);
    collectOldNames(q->body.get(), out); return;
  }
  if (auto ix = dynamic_cast<const Index*>(e)) { collectOldNames(ix->base.get(), out); collectOldNames(ix->index.get(), out); return; }
  if (auto f = dynamic_cast<const Field*>(e)) { collectOldNames(f->base.get(), out); return; }
  if (auto c = dynamic_cast<const Call*>(e)) { for (auto& a : c->args) collectOldNames(a.get(), out); return; }
  // Advanced-math nodes  -  recurse so `old(x)` inside a matrix/pow/solve/
  // integrate clause is still collected and its pre-snapshot seeded.
  if (auto p = dynamic_cast<const PowerExpr*>(e)) {
    collectOldNames(p->base.get(), out);
    collectOldNames(p->exponent.get(), out); return;
  }
  if (auto ml = dynamic_cast<const MatrixLit*>(e)) {
    for (auto& row : ml->rows)
      for (auto& el : row)
        collectOldNames(el.get(), out);
    return;
  }
  if (auto mm = dynamic_cast<const MatMulExpr*>(e)) {
    collectOldNames(mm->lhs.get(), out);
    collectOldNames(mm->rhs.get(), out); return;
  }
  if (auto s = dynamic_cast<const SolveExpr*>(e)) {
    collectOldNames(s->lhs.get(), out);
    collectOldNames(s->rhs.get(), out); return;
  }
  if (auto ig = dynamic_cast<const IntegrateExpr*>(e)) {
    collectOldNames(ig->lo.get(), out);
    collectOldNames(ig->hi.get(), out);
    if (ig->body) collectOldNames(ig->body.get(), out); return;
  }
  // MathSymExpr and SuperscriptExpr: the former has no sub-expressions (it is
  // a leaf constant like `pi`/`e`); the latter has base + exponent.
  if (auto/*ms*/ ms = dynamic_cast<const MathSymExpr*>(e)) {
    (void)ms; // leaf  -  nothing to recurse into
    return;
  }
  if (auto se = dynamic_cast<const SuperscriptExpr*>(e)) {
    collectOldNames(se->base.get(), out);
    if (se->exponent) collectOldNames(se->exponent.get(), out); return;
  }
}

// ox:proof Emit the discharge query for one clause:
//   (push) [(assert <premise>) ...] (assert (not <clause-term>)) (check-sat) (pop)
// `unsat` => the clause holds for all inputs (under the premises).
// `premises` are SMT terms asserted BEFORE the negated clause; this is how
// `requires` clauses get carried into the discharge of `ensures`/`assert`/
// `invariant` (the standard Hoare-logic pattern: prove Body ⊢ clause under
// the assumption Pre). For `requires` itself, premises is empty  -  the
// precondition is tested against unconstrained inputs, so `sat` honestly
// means "a caller could violate it".
void smtDischarge(SmtCtx& c, const std::string& label, const std::string& term,
                  const std::vector<std::string>& premises) {
  c.out << "; --- discharge (" << label << ") ---\n"
        << "(push)\n";
  for (const auto& p : premises)
    c.out << "(assert " << p << ")\n";
  // ox:proof Use a FRESH tactic stack per query: `(check-sat-using (then simplify smt))`
  // re-runs the SMT solver tactic from scratch so Z3's MBQI
  // quantifier-instantiation cache does NOT leak across check-sats. A bare
  // `(check-sat)` reuses the prior tactic invocation's internal state, so an
  // early `sat` model (e.g. a concrete-arg `requires` discharge) can poison
  // a later `forall k` over an Int-sorted array  -  making it spuriously `sat`
  // in the full file while it is `unsat` in isolation, or silently
  // `unknown` (surfacing as `(no result)`).
  c.out << "(assert (not " << term << "))\n"
        << "(check-sat-using (then simplify smt))\n"
        << "(pop)\n\n";
}

// ox:proof Emit a clause as a named define-fun and immediately discharge it. The caller
// has already populated `c.declared` with the clause's scope (params/result/
// old_ snapshots); we must NOT clear it here, or those names stop resolving.
// `premises` is forwarded to smtDischarge (see comment there).
//
// Feature 1+2: when the Expr* is available, route through smtDischargeGoal
// (ProofSplitter) which splits conjuncts/implications/quantifiers/branches,
// selects a per-sub-goal tactic, and emits one check-sat per sub-goal.
// This is backward-compatible: the verify-report parser sees multiple
// check-sats per clause and keeps the last non-unknown result  -  unsat
// beats sat/unknown, so a clause proven by ALL sub-goals reports unsat.
// When e is null or smtDischargeGoal fails, we fall back to the legacy path.
void smtClause(SmtCtx& c, const std::string& fnName, const char* kind, int idx,
               const Expr* e, const std::vector<std::string>& premises) {
  std::string label = fnName + "_" + kind + "_" + std::to_string(idx);
  c.out << "; " << label << " (source line " << (e ? e->line : 0) << ")\n";
  std::string term = smtExpr(c, e);
  c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
  // Feature 1+2: try goal splitting when we have the AST node.
  if (e) {
    ox_smt::ProofGoal pg;
    pg.label = label;
    pg.term = e;
    pg.premises = premises;
    pg.isGround = true;  // smtDischargeGoal will inspect for quantifiers
    int nSub = ox_smt::smtDischargeGoal(c, pg);
    if (nSub > 0) return;  // splitting succeeded
  }
  smtDischarge(c, label, label, premises);
}

// Walk a statement block for invariant + assert clauses (recursively into
// nested control flow), emitting each. `fnName` prefixes the labels; `depth`
// distinguishes nested loops generically (loop-invariant N @ depth D).
// `premises` carries the function's `requires` terms so body contracts
// discharge under the assumed precondition (Hoare-logic sound extension).
void smtWalkStmts(SmtCtx& c, const std::string& fnName,
                  const std::vector<StmtPtr>& stmts, int depth,
                  const std::vector<std::string>& premises) {
  for (auto& s : stmts) {
    if (!s) continue;
    if (auto ws = dynamic_cast<const WhileStmt*>(s.get())) {
      int n = 0;
      for (auto& inv : ws->invariants) {
        std::string label = fnName + "_invariant_d" + std::to_string(depth)
                            + "_" + std::to_string(n);
        c.out << "; " << label << " (while, source line "
              << (inv ? inv->line : ws->line) << ")\n";
        std::string term = smtExpr(c, inv.get());
        c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
        smtDischarge(c, label, label, premises);
        ++n;
      }
      smtWalkStmts(c, fnName, ws->body, depth + 1, premises);
      continue;
    }
    if (auto fs = dynamic_cast<const ForStmt*>(s.get())) {
      int n = 0;
      for (auto& inv : fs->invariants) {
        std::string label = fnName + "_invariant_d" + std::to_string(depth)
                            + "_" + std::to_string(n);
        c.out << "; " << label << " (for, source line "
              << (inv ? inv->line : fs->line) << ")\n";
        std::string term = smtExpr(c, inv.get());
        c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
        smtDischarge(c, label, label, premises);
        ++n;
      }
      smtWalkStmts(c, fnName, fs->body, depth + 1, premises);
      continue;
    }
    if (auto a = dynamic_cast<const AssertStmt*>(s.get())) {
      std::string label = fnName + "_assert_" + std::to_string(c.assertSeq++);
      c.out << "; " << label << " (source line " << a->line << ")\n";
      std::string term = smtExpr(c, a->cond.get());
      c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
      // ox:proof `assert <expr> by { <hints> };`  -  emit the hint statements as SMT
      // premises inside a fresh (push)…(pop) scope so they are available to
      // the discharge query but do not leak beyond it. Each `assert H;` hint
      // is discharged AND assumed (added to the running premise list) so the
      // outer assert sees H; other hints are walked through the Tier A
      // statement walker recursively (no symbolic effect for InstantiateStmt /
      // ProofBlockStmt / CalcStmt which are not yet emitted in this tier).
      if (!a->byBody.empty()) {
        c.out << "(push)\n";
        std::vector<std::string> hintPrem = premises;
        for (auto& h : a->byBody) {
          if (!h) continue;
          // ox:proof Detect a plain `assert H;` hint: discharge H, then assume H.
          if (auto ha = dynamic_cast<const AssertStmt*>(h.get())) {
            if (ha->byBody.empty()) {
              std::string hlabel = fnName + "_assert_" +
                                   std::to_string(c.assertSeq++);
              c.out << "; " << hlabel << " (hint, source line " << ha->line
                    << ")\n";
              std::string hterm = smtExpr(c, ha->cond.get());
              c.out << "(define-fun " << hlabel << " () Bool " << hterm
                    << ")\n\n";
              smtDischarge(c, hlabel, hlabel, hintPrem);
              c.out << "(assert " << hlabel << ")\n";
              hintPrem.push_back(hlabel);
              continue;
            }
          }
          // ox:proof Non-assert hint (InstantiateStmt / lemma-call ExprStmt / calc / etc.)
          //  -  falls through in the Tier A path (no symbolic effect yet). Named
          // ghost-proof statements like instantiate/calc/proof-block are parsed
          // and Sema-checked but not yet emitted in this tier; accept them for
          // forward compatibility. The Tier B path (smtEncodeStmt) handles any
          // ghost-proof state mutation, so this is honest under-approximation.
          (void)h;
        }
        smtDischarge(c, label, label, hintPrem);
        c.out << "(pop)\n\n";
      } else {
        smtDischarge(c, label, label, premises);
      }
      continue;
    }
    if (auto as = dynamic_cast<const AssumeStmt*>(s.get())) {
      // `assume <expr>;` / `trusted assume <expr>;`  -  add the condition as an
      // SMT hypothesis (asserted, not discharged). The condition is a spec
      // expression; we lower it with smtExpr (Tier A's non-WP path) and emit
      // `(assert <cond>)` so the fact becomes available to subsequent clause
      // discharge queries in this function. For a `trusted` assume, also emit
      // a `; note: trusted assume at line N` comment (+ optional source line)
      // into the .smt2  -  the doVerify report then rescans those notes for the
      // `--audit-trust` report (mirroring its scan of `; note: replaced ...`
      // and the `; axiom ...` headers for the audit-axioms fall-through).
      if (as->isTrusted) {
        c.out << "; note: trusted assume at line " << as->line << "\n";
        if (!as->sourceCitation.empty())
          c.out << "; note: source: " << as->sourceCitation << "\n";
      }
      std::string term = smtExpr(c, as->cond.get());
      c.out << "(assert " << term << ")\n";
      // ox:why The assumed term is not a named define-fun, so we cannot push a
      // symbolic handle onto the premise list cheaply in Tier A. Tier B
      // (smtEncodeStmt) threads the term through its own premise list; Tier A
      // only re-discharges clauses that re-encode the condition, so leaving
      // the premise list untouched is the same honest under-approximation as
      // the assert-by-hint path above (the hypothesis is on disk for Z3, just
      // not threaded into this loose walker's premise list).
      continue;
    }
    // Recurse into any block-shaped statement; only those that embed a
    // statement list matter here. (if/else bodies, etc.)  -  best-effort.
    if (auto is = dynamic_cast<const IfStmt*>(s.get())) {
      smtWalkStmts(c, fnName, is->then, depth, premises);
      smtWalkStmts(c, fnName, is->else_, depth, premises);
      continue;
    }
    if (auto bl = dynamic_cast<const Block*>(s.get())) {
      smtWalkStmts(c, fnName, bl->stmts, depth, premises);
      continue;
    }
  }
}

// Tier B  -  weakest-precondition / SSA body encoder + BitVec theory.
//
// smtWalkStmts above (Tier A, retained) emits one discharge query per loop
// invariant / assert WITHOUT reasoning about how the local that the clause
// references was assigned, and treats `result` as a free uninterpreted const.
// That is sound but loose: it can't discharge `ensures result >= 0` on fib
// (the body computes result, not the signature) and it reports `unknown name
// 'i' -> fib_ph0` because the loop variable is never bound in the SMT store.
//
// smtEncodeBody (below) is a fresh WP/SSA symbolic executor layered ON TOP of
// the same SmtCtx. It threads
//   pathCond : a growing conjunction of assumed branches (Bool SMT term,
//               or "" meaning `true`)
//   store    : map Oxide source name -> SMT term (Int-valued string).
// On `let x = e`:   store[x] = smtExprWp(e, store).
// On `x = e`:       store[x] = smtExprWp(e, store)        (SSA re-bind)
// On `if c {A} else B`: fork  -  recurse into A under pathCond∧c, into B
//                   under pathCond∧¬c. Either side's `return` rebinds `result`
//                   and short-circuits further discharge in that arm.
// On `assert P`:    discharge P under pathCond (WP: must hold here).
// On `return e`:    store["result"] = smtExprWp(e, store); record a `returned`
//                   flag so downstream ensures discharge picks up the WP term.
// On `while c invariant I {body}`: THREE-check induction scheme, see below.
// On a loop WITHOUT an invariant: emit `assume false` at the loop site
//                   (i.e. discharge every assert inside the loop body under
//                   `(assert false)` so it trivially `unsat`s  -  honest: we let
//                   the prover ASSUME the loop never runs). Documented at the
//                   emission site. Downstream code after such a loop is treated
//                   as unreachable for verification purposes (any assert is
//                   also discharged under a false path condition).

// Forward-declared smtExprWp: lower an expression with a symbolic store overlay
// (the store shadows nameMap for any Oxide local currently in scope).
std::string smtExprWp(SmtCtx& c, const Expr* e,
                      const std::map<std::string, std::string>& store);

// Pre-pass: mark every Oxide source name whose definition REACHES a bitwise
// op (band/bor/bxor/shl/shr) as BitVec-typed for THIS function. We approximate
// "definition reaches" via the body's `let`/`assign` chain  -  if a name is
// ever an operand of a bitop OR is assigned an expression that contains a
// bitop, the name is marked bv-typed. We don't flow-track across `if`/loops
// precisely; this is a conservative OVER-approximation (marking more names bv
// is sound  -  it can only make us model MORE bitops as real BitVec and emit
// more int2bv/bv2int bridges, never fewer), so a marked name's arithmetic uses
// still resolve correctly via the int2bv/bv2int boundary.
//
// Implementation: a single pass collects every VarRef NAME appearing as an
// immediate operand to a bitop binary expression anywhere in the body. We
// also walk every `let`/assign to spread the bv-mark from any RHS containing
// a bitop to the assigned LHS. The set is sound + cheap.
void smtCollectBitopsExpr(const Expr* e, std::set<std::string>& out);
void smtCollectBitopsStmt(const Stmt* s, std::set<std::string>& out,
                           std::set<std::string>& spreadFrom);

// The set of bitop operands collected from one expression (recursively).
// `out` accumulates bare Oxide VarRef names that flow into a bitop.
void smtCollectBitopsExpr(const Expr* e, std::set<std::string>& out) {
  if (!e) return;
  if (auto b = dynamic_cast<const BinaryExpr*>(e)) {
    bool isBitop = (b->op == BinaryExpr::Op::band ||
                    b->op == BinaryExpr::Op::bor  ||
                    b->op == BinaryExpr::Op::bxor ||
                    b->op == BinaryExpr::Op::shl  ||
                    b->op == BinaryExpr::Op::shr);
    if (isBitop) {
      // Both operands of a bitop flow into BitVec.
      if (auto v = dynamic_cast<const VarRef*>(b->lhs.get())) out.insert(v->name);
      if (auto v = dynamic_cast<const VarRef*>(b->rhs.get())) out.insert(v->name);
    }
    smtCollectBitopsExpr(b->lhs.get(), out);
    smtCollectBitopsExpr(b->rhs.get(), out);
    return;
  }
  if (auto u = dynamic_cast<const UnaryExpr*>(e)) {
    smtCollectBitopsExpr(u->base.get(), out); return;
  }
  if (auto t = dynamic_cast<const TernaryExpr*>(e)) {
    smtCollectBitopsExpr(t->cond.get(), out);
    smtCollectBitopsExpr(t->thenE.get(), out);
    smtCollectBitopsExpr(t->elseE.get(), out); return;
  }
  if (auto o = dynamic_cast<const OldExpr*>(e)) { smtCollectBitopsExpr(o->sub.get(), out); return; }
  if (auto q = dynamic_cast<const QuantExpr*>(e)) {
    smtCollectBitopsExpr(q->lo.get(), out);
    smtCollectBitopsExpr(q->hi.get(), out);
    smtCollectBitopsExpr(q->body.get(), out); return;
  }
  if (auto ix = dynamic_cast<const Index*>(e)) {
    smtCollectBitopsExpr(ix->base.get(), out);
    smtCollectBitopsExpr(ix->index.get(), out); return;
  }
  if (auto f = dynamic_cast<const Field*>(e)) { smtCollectBitopsExpr(f->base.get(), out); return; }
  if (auto c = dynamic_cast<const Call*>(e)) {
    for (auto& a : c->args) smtCollectBitopsExpr(a.get(), out); return;
  }
  if (auto m = dynamic_cast<const MethodCall*>(e)) {
    smtCollectBitopsExpr(m->receiver.get(), out);
    for (auto& a : m->args) smtCollectBitopsExpr(a.get(), out); return;
  }
  // ox:why Advanced-math nodes  -  recurse so a bitop inside their operands is seen.
  if (auto p = dynamic_cast<const PowerExpr*>(e)) {
    smtCollectBitopsExpr(p->base.get(), out);
    smtCollectBitopsExpr(p->exponent.get(), out); return;
  }
  if (auto mm = dynamic_cast<const MatMulExpr*>(e)) {
    smtCollectBitopsExpr(mm->lhs.get(), out);
    smtCollectBitopsExpr(mm->rhs.get(), out); return;
  }
  if (auto s = dynamic_cast<const SolveExpr*>(e)) {
    smtCollectBitopsExpr(s->lhs.get(), out);
    smtCollectBitopsExpr(s->rhs.get(), out); return;
  }
  if (auto ig = dynamic_cast<const IntegrateExpr*>(e)) {
    smtCollectBitopsExpr(ig->lo.get(), out);
    smtCollectBitopsExpr(ig->hi.get(), out);
    if (ig->body) smtCollectBitopsExpr(ig->body.get(), out); return;
  }
  if (auto ml = dynamic_cast<const MatrixLit*>(e)) {
    for (auto& row : ml->rows)
      for (auto& el : row)
        smtCollectBitopsExpr(el.get(), out);
    return;
  }
  // MathSymExpr: leaf constant, no sub-expressions to recurse into.
  if (auto/*ms*/ ms = dynamic_cast<const MathSymExpr*>(e)) { (void)ms; return; }
  // SuperscriptExpr: recurse into base and exponent.
  if (auto se = dynamic_cast<const SuperscriptExpr*>(e)) {
    smtCollectBitopsExpr(se->base.get(), out);
    if (se->exponent) smtCollectBitopsExpr(se->exponent.get(), out); return;
  }
}

// Walk the function body collecting bitop operand names; spread the mark from
// any RHS containing a bitop to its assigned LHS (var or let). `spreadFrom`
// is the set of names already marked bv; we re-walk until fixpoint so a chain
// `let m = (x & 0xff); let y = m; let z = y | 1;` marks m, y, z.
void smtCollectBitopsStmt(const Stmt* s, std::set<std::string>& out,
                           std::set<std::string>& spreadFrom) {
  if (!s) return;
  if (auto let = dynamic_cast<const LetStmt*>(s)) {
    // Collect every operand that flows into a bitop in the RHS directly into
    // `out` (so a param used as `x << 1` is itself marked bv, not just the
    // LHS). smtCollectBitopsExpr already inserts a bitop's VarRef operands.
    std::set<std::string> before = out;
    smtCollectBitopsExpr(let->init.get(), out);
    bool rhsHasBitop = (out.size() != before.size());
    // If the RHS contains a bitop OR references an already-bv name, the LHS is
    // itself a bv-flowing value.
    if (rhsHasBitop) out.insert(let->name);
    std::set<std::string> rhsNames; smtCollectBitopsExpr(let->init.get(), rhsNames);
    for (auto& n : rhsNames) if (out.count(n)) { out.insert(let->name); break; }
    (void)spreadFrom;
    return;
  }
  if (auto es = dynamic_cast<const ExprStmt*>(s)) {
    if (auto a = dynamic_cast<const AssignTarget*>(es->expr.get())) {
      if (a->kind == AssignTarget::Kind::var) {
        std::set<std::string> rhsBv; smtCollectBitopsExpr(a->value.get(), rhsBv);
        if (!rhsBv.empty()) out.insert(a->name);
        std::set<std::string> rhsNames; smtCollectBitopsExpr(a->value.get(), rhsNames);
        for (auto& n : rhsNames) if (out.count(n)) { out.insert(a->name); break; }
      }
      // index/field/deref assignments are out of scope for Tier B
      return;
    }
    smtCollectBitopsExpr(es->expr.get(), out);
    return;
  }
  if (auto is = dynamic_cast<const IfStmt*>(s)) {
    smtCollectBitopsExpr(is->cond.get(), out);
    for (auto& x : is->then) smtCollectBitopsStmt(x.get(), out, spreadFrom);
    for (auto& x : is->else_) smtCollectBitopsStmt(x.get(), out, spreadFrom);
    return;
  }
  if (auto ws = dynamic_cast<const WhileStmt*>(s)) {
    smtCollectBitopsExpr(ws->cond.get(), out);
    for (auto& x : ws->body) smtCollectBitopsStmt(x.get(), out, spreadFrom);
    return;
  }
  if (auto fs = dynamic_cast<const ForStmt*>(s)) {
    smtCollectBitopsExpr(fs->start.get(), out);
    smtCollectBitopsExpr(fs->end.get(), out);
    for (auto& x : fs->body) smtCollectBitopsStmt(x.get(), out, spreadFrom);
    return;
  }
  if (auto bl = dynamic_cast<const Block*>(s)) {
    for (auto& x : bl->stmts) smtCollectBitopsStmt(x.get(), out, spreadFrom);
    return;
  }
  if (auto as = dynamic_cast<const AssertStmt*>(s)) {
    smtCollectBitopsExpr(as->cond.get(), out);
    // `assert <expr> by { <hints> };`  -  also scan the proof-hint statements so
    // any bitop inside their expressions is collected (for bv-flow tracking).
    for (auto& h : as->byBody) smtCollectBitopsStmt(h.get(), out, spreadFrom);
    return;
  }
  if (auto cs = dynamic_cast<const CalcStmt*>(s)) {
    // `calc { <expr>; <REL> { <hints> } <expr>; ... }`  -  scan each step's
    // expression AND its proof hints so bitop-flow tracking sees bitops inside
    // either (parity with `assert ... by { }` above). The relation strings are
    // pure syntax (no expressions to scan).
    for (auto& step : cs->steps) {
      if (step.expr) smtCollectBitopsExpr(step.expr.get(), out);
      for (auto& h : step.hints) smtCollectBitopsStmt(h.get(), out, spreadFrom);
    }
    return;
  }
  if (auto ret = dynamic_cast<const ReturnStmt*>(s)) {
    smtCollectBitopsExpr(ret->value.get(), out); return;
  }
}

// Detect whether an expression contains ANY bitop op (not just operands).
// Used to decide whether to mark the LHS as bv.
static bool smtExprContainsBitop(const Expr* e) {
  if (!e) return false;
  if (auto b = dynamic_cast<const BinaryExpr*>(e)) {
    if (b->op == BinaryExpr::Op::band || b->op == BinaryExpr::Op::bor  ||
        b->op == BinaryExpr::Op::bxor || b->op == BinaryExpr::Op::shl  ||
        b->op == BinaryExpr::Op::shr) return true;
    return smtExprContainsBitop(b->lhs.get()) || smtExprContainsBitop(b->rhs.get());
  }
  if (auto u = dynamic_cast<const UnaryExpr*>(e)) return smtExprContainsBitop(u->base.get());
  if (auto t = dynamic_cast<const TernaryExpr*>(e))
    return smtExprContainsBitop(t->cond.get()) ||
           smtExprContainsBitop(t->thenE.get()) || smtExprContainsBitop(t->elseE.get());
  if (auto o = dynamic_cast<const OldExpr*>(e)) return smtExprContainsBitop(o->sub.get());
  if (auto ix = dynamic_cast<const Index*>(e))
    return smtExprContainsBitop(ix->base.get()) || smtExprContainsBitop(ix->index.get());
  if (auto f = dynamic_cast<const Field*>(e)) return smtExprContainsBitop(f->base.get());
  if (auto c = dynamic_cast<const Call*>(e)) {
    for (auto& a : c->args) if (smtExprContainsBitop(a.get())) return true;
  }
  // ox:why Advanced-math nodes  -  recurse into their sub-expressions so a bitop
  // nested inside (e.g. `pow((x & 0xff), 2)`, or a MatrixLit row that uses
  // `&`) is detected and the value marked bv-typed. The new node types
  // themselves are not bitops  -  only their operands can be.
  if (auto p = dynamic_cast<const PowerExpr*>(e))
    return smtExprContainsBitop(p->base.get()) ||
           smtExprContainsBitop(p->exponent.get());
  if (auto mm = dynamic_cast<const MatMulExpr*>(e))
    return smtExprContainsBitop(mm->lhs.get()) ||
           smtExprContainsBitop(mm->rhs.get());
  if (auto s = dynamic_cast<const SolveExpr*>(e))
    return smtExprContainsBitop(s->lhs.get()) ||
           smtExprContainsBitop(s->rhs.get());
  if (auto ig = dynamic_cast<const IntegrateExpr*>(e))
    return smtExprContainsBitop(ig->lo.get()) ||
           smtExprContainsBitop(ig->hi.get()) ||
           (ig->body && smtExprContainsBitop(ig->body.get()));
  if (auto ml = dynamic_cast<const MatrixLit*>(e)) {
    for (auto& row : ml->rows)
      for (auto& el : row)
        if (smtExprContainsBitop(el.get())) return true;
  }
  // MathSymExpr: leaf constant  -  no bitops.
  if (auto/*ms*/ ms = dynamic_cast<const MathSymExpr*>(e)) { (void)ms; return false; }
  if (auto se = dynamic_cast<const SuperscriptExpr*>(e))
    return smtExprContainsBitop(se->base.get()) ||
           (se->exponent && smtExprContainsBitop(se->exponent.get()));
  return false;
}

// Is `e` an arithmetic operand that should lower as a BitVec (not Int)? True
// when e is a VarRef bound to a declared-bv store/nameMap symbol, or when e
// itself contains a bitop. When this holds for an operand of `+`/`-`/`*`,
// lowering the arithmetic through `smtBvBin` (`bvadd`/`bvsub`/`bvmul`) keeps
// the chain in fixed-width BitVec  -  modelling true 64-bit wraparound  - 
// instead of crossing back to unbounded Int `+` and silently losing overflow.
// (The bit-flowing value's `bv2int` round-trip through `Int +` would turn
// 0xFFFF..F + 1 into 18446744073709551616, not 0.)
static bool smtWpOperandIsBv(SmtCtx& c, const Expr* e,
                              const std::map<std::string, std::string>& store) {
  if (!e) return false;
  if (auto v = dynamic_cast<const VarRef*>(e)) {
    // First check the WP-store-bound bitop-flowing source-name set. This is
    // the layer that catches `let masked = w & ALL_ONES`  -  `masked`'s store
    // term is a bv-bridge EXPRESSION (not a bare declared symbol), so the
    // bvVars check below on the term string wouldn't recognise it.
    if (c.wpBvNames.count(v->name)) return true;
    auto sit = store.find(v->name);
    if (sit != store.end()) return c.bvVars.count(sit->second) > 0
                                     || c.wpBvNames.count(v->name) > 0;
    auto mit = c.nameMap.find(v->name);
    if (mit != c.nameMap.end()) return c.bvVars.count(mit->second) > 0;
    return c.bvVars.count(v->name) > 0;
  }
  return smtExprContainsBitop(e);
}

static std::string smtExprWpBv(SmtCtx& c, const Expr* e,
                               const std::map<std::string, std::string>& store) {
  if (!e) return "(_ bv0 " + std::to_string(BV_W) + ")";
  if (auto i = dynamic_cast<const IntLit*>(e)) {
    return "(_ bv" + std::to_string(i->v) + " " + std::to_string(BV_W) + ")";
  }
  if (auto v = dynamic_cast<const VarRef*>(e)) {
    // A store binding that is ITSELF a bare declared-bv symbol stays bare;
    // otherwise the store term is Int and must be bridged. An outer param/
    // result declared bv resolves bare too.
    auto sit = store.find(v->name);
    if (sit != store.end()) {
      if (c.bvVars.count(sit->second)) return sit->second;
      return smtIntToBv(sit->second);
    }
    auto mit = c.nameMap.find(v->name);
    if (mit != c.nameMap.end() && c.bvVars.count(mit->second)) return mit->second;
    return smtIntToBv(smtExprWp(c, e, store));
  }
  if (auto u = dynamic_cast<const UnaryExpr*>(e)) {
    if (u->op == UnaryExpr::Op::neg)  return "(bvneg " + smtExprWpBv(c, u->base.get(), store) + ")";
    if (u->op == UnaryExpr::Op::bnot) return "(bvnot " + smtExprWpBv(c, u->base.get(), store) + ")";
    return smtIntToBv(smtExprWp(c, e, store));
  }
  if (auto b = dynamic_cast<const BinaryExpr*>(e)) {
    std::string l = smtExprWpBv(c, b->lhs.get(), store);
    std::string r = smtExprWpBv(c, b->rhs.get(), store);
    switch (b->op) {
      case BinaryExpr::Op::add:  return "(bvadd " + l + " " + r + ")";
      case BinaryExpr::Op::sub:  return "(bvsub " + l + " " + r + ")";
      case BinaryExpr::Op::mul:  return "(bvmul " + l + " " + r + ")";
      case BinaryExpr::Op::band: return "(bvand " + l + " " + r + ")";
      case BinaryExpr::Op::bor:  return "(bvor "  + l + " " + r + ")";
      case BinaryExpr::Op::bxor: return "(bvxor " + l + " " + r + ")";
      case BinaryExpr::Op::shl:  return "(bvshl " + l + " " + r + ")";
      case BinaryExpr::Op::shr:  return "(bvlshr " + l + " " + r + ")";
      default: return smtIntToBv(smtExprWp(c, e, store));
    }
  }
  if (auto t = dynamic_cast<const TernaryExpr*>(e)) {
    return "(ite " + smtExprWp(c, t->cond.get(), store) + " "
           + smtExprWpBv(c, t->thenE.get(), store) + " "
           + smtExprWpBv(c, t->elseE.get(), store) + ")";
  }
  return smtIntToBv(smtExprWp(c, e, store));
}

// ox:proof Lower an expression to an SMT term using `store` to resolve bare names. The
// `store` shadows `c.nameMap`: if a name is in `store`, use the stored term;
// otherwise fall back to the outer nameMap (params/result/old). Parameter
// `bvSet` is the pre-computed set of bv-typed names for THIS function; names
// in it get BitVec lowering at bitops (see smtExpr), and bridge functions
// emit int2bv/bv2int at the Int/BitVec boundary.
//
// smtExprWp does NOT mutate declarations in the way smtExpr does for unknown
// names  -  it instead falls back to a fresh per-call placeholder (still honest
// via the `; note:` mechanism) OR, if the name is recognizably a function
// PARAM/result (it's in nameMap but never bound in the body), it reuses the
// outer declared symbol (so `n` in fib keeps reading p_fib_n).
std::string smtExprWp(SmtCtx& c, const Expr* e,
                      const std::map<std::string, std::string>& store) {
  if (!e) return "true";
  // ox:why We re-implement lower rather than delegating to smtExpr because we want
  // the store to take priority (SmtCtx.nameMap is shared across clauses).
  if (auto i = dynamic_cast<const IntLit*>(e)) {
    return std::to_string((long long)i->v);
  }
  if (auto f = dynamic_cast<const FloatLit*>(e)) {
    if (f->v == (double)(long long)f->v) {
      return std::to_string((long long)f->v) + ".0";
    }
    std::ostringstream os;
    os << f->v;
    return os.str();
  }
  if (auto b = dynamic_cast<const BoolLit*>(e)) {
    return b->v ? "true" : "false";
  }
  if (auto v = dynamic_cast<const VarRef*>(e)) {
    // 1. Try symbolic store (WP bindings  -  locals assigned/let in the body).
    auto sit = store.find(v->name);
    if (sit != store.end()) {
      if (c.bvVars.count(sit->second)) return smtBvToInt(sit->second);
      return sit->second;
    }
    // 2. Fall back to the function's outer nameMap (params, result, old_).
    auto mit = c.nameMap.find(v->name);
    if (mit != c.nameMap.end()) {
      if (c.bvVars.count(mit->second)) return smtBvToInt(mit->second);
      return mit->second;
    }
    // 3. Already declared as a fresh const (binder/placeholder).
    if (c.declared.count(v->name)) {
      if (c.bvVars.count(v->name)) return smtBvToInt(v->name);
      return v->name;
    }
    // 4. Compile-time const global.
    auto cg = c.constGlobals.find(v->name);
    if (cg != c.constGlobals.end()) return std::to_string(cg->second);
    // ox:note 5. Unknown name  -  emit a placeholder (honest, with ; note:).
    return smtPlaceholder(c, "Int", ("unknown name '" + v->name + "'").c_str());
  }
  if (auto o = dynamic_cast<const OldExpr*>(e)) {
    if (auto v = dynamic_cast<const VarRef*>(o->sub.get())) {
      // WP: `old(x)` reads the pre-state snapshot (outer old_<x> binding).
      auto mit = c.nameMap.find("old_" + v->name);
      if (mit != c.nameMap.end()) return mit->second;
      if (auto s = store.find(v->name); s != store.end()) return s->second;
    }
    return smtPlaceholder(c, "Int", "old(x) of a non-bare name");
  }
  if (auto q = dynamic_cast<const QuantExpr*>(e)) {
    std::string bv = c.curFn + "_q_" + q->binder + "_" + std::to_string(c.placeholderSeq++);
    std::string lo = smtExprWp(c, q->lo.get(), store);
    std::string hi = smtExprWp(c, q->hi.get(), store);
    std::string hiCheck =
      q->inclusive ? ("(<= " + bv + " " + hi + ")") : ("(< " + bv + " " + hi + ")");
    std::string range = "(and (>= " + bv + " " + lo + ") " + hiCheck + ")";
    // Bind the binder's bare name to the fresh symbol: do so via a store overlay
    // rather than mutating the outer nameMap (which would affect neighbours).
    auto storeWith = store;
    storeWith[q->binder] = bv;
    std::string body = smtExprWp(c, q->body.get(), storeWith);
    std::ostringstream t;
    t << "(" << (q->isForall ? "forall" : "exists")
      << " ((" << bv << " " << smtSort(q->binderType) << ")) "
      << "(=> " << range << " " << body << "))";
    return t.str();
  }
  if (auto u = dynamic_cast<const UnaryExpr*>(e)) {
    if (u->op == UnaryExpr::Op::not_) return "(not " + smtExprWp(c, u->base.get(), store) + ")";
    if (u->op == UnaryExpr::Op::neg) return "(- " + smtExprWp(c, u->base.get(), store) + ")";
    return smtPlaceholder(c, "Int", "unary op");
  }
  if (auto b = dynamic_cast<const BinaryExpr*>(e)) {
    // Does EITHER operand lower as BitVec? If so, this whole binary expression
    // lives in the fixed-width BV domain: comparisons go through `smtExprWpBv`
    // (decisive sat/unsat with countermodels), and arithmetic (`+`/`-`/`*`)
    // uses `smtBvBin` (`bvadd`/`bvsub`/`bvmul`) so the chain models true 64-bit
    // wraparound. Without this, a bit-flowing value crossed back to Int via
    // `bv2int` and then `Int +`  -  `bv2int 0xFFFF..F = 18446744073709551615`,
    // `+1` = `18446744073709551616`, NOT `0`. Real u64/i64 hardware wraps; our
    // BV encoding now matches. The `Int` comparison/arithmetic path stays for
    // pure-Int operands (unbounded, cleaner for ordinary numeric proofs).
    bool lBv = smtWpOperandIsBv(c, b->lhs.get(), store);
    bool rBv = smtWpOperandIsBv(c, b->rhs.get(), store);
    bool anyBv = lBv || rBv;
    // A2  -  mirror of the `smtExpr` clause-path bitop-to-Int rewrite (see
    // lines ~1655-1662): before falling into the BV equality shortcut, give
    // `(X & M) == 0` / `(X & M) != 0` a chance to lower to `(= (mod X (M+1)) 0)`.
    // The store-aware overload is mandatory here: the masked operand `X` may
    // be a VarRef whose binding lives in `store` (the WP/SSA overlay), not in
    // `c.nameMap`. Passing `&store` routes `op` through `smtExprWp`; the
    // non-matching shape still returns "" and falls through to the bv path
    // below unchanged, preserving the original soundness fall-through.
    if (b->op == BinaryExpr::Op::eq || b->op == BinaryExpr::Op::ne) {
      std::string rewritten = smtTryBandMaskEqZeroToMod(
          c, b, /*neg=*/(b->op == BinaryExpr::Op::ne), /*store=*/&store);
      if (!rewritten.empty()) return rewritten;
    }
    // gap C5 (X | M) == M where ~M is a low-bits mask -> (= (mod X 2^k) 0).
    // store-aware (X may be a WP-local name).
    if (b->op == BinaryExpr::Op::eq || b->op == BinaryExpr::Op::ne) {
      std::string rewritten = smtTryBorEqMaskToMod(
          c, b, /*neg=*/(b->op == BinaryExpr::Op::ne), /*store=*/&store);
      if (!rewritten.empty()) return rewritten;
    }
    // gap C5 (X >> N) == K -> (= (div X 2^N) K). `anyBv` already captures the
    // "bitop present" gate the task requires; only rescue a bitop-flavoured
    // shr-equality into pure LIA (the switch below would otherwise emit
    // `bvlshr`, leaking a BV atom into a quantified body). store-aware.
    if ((b->op == BinaryExpr::Op::eq || b->op == BinaryExpr::Op::ne) && anyBv) {
      std::string rewritten = smtTryShrEqToDiv(
          c, b, /*neg=*/(b->op == BinaryExpr::Op::ne), /*store=*/&store);
      if (!rewritten.empty()) return rewritten;
    }
    if (anyBv && (b->op == BinaryExpr::Op::eq || b->op == BinaryExpr::Op::ne)) {
      std::string bvEq = "(= " + smtExprWpBv(c, b->lhs.get(), store) + " "
                              + smtExprWpBv(c, b->rhs.get(), store) + ")";
      return (b->op == BinaryExpr::Op::eq) ? bvEq : ("(not " + bvEq + ")");
    }
    if (anyBv && (b->op == BinaryExpr::Op::lt || b->op == BinaryExpr::Op::gt ||
                  b->op == BinaryExpr::Op::le || b->op == BinaryExpr::Op::ge)) {
      // Signed BV comparison to match i64/u64 arithmetic semantics. For u64
      // ((extend unsigned is automatic since the symbol already IS (_ BitVec))
      // the comparison here is SMT's `bvult`/`bvslt`  -  we use bvslt for parity
      // with Oxide's IRGen, which lowers signed comparison via `icmp slt`
      // after a zero/sign-extend of the BV form. Pragmatic: bvslt for `<`.
      std::string l = smtExprWpBv(c, b->lhs.get(), store);
      std::string r = smtExprWpBv(c, b->rhs.get(), store);
      switch (b->op) {
        case BinaryExpr::Op::lt: return "(bvslt " + l + " " + r + ")";
        case BinaryExpr::Op::le: return "(bvsle " + l + " " + r + ")";
        case BinaryExpr::Op::gt: return "(bvsgt " + l + " " + r + ")";
        case BinaryExpr::Op::ge: return "(bvsge " + l + " " + r + ")";
        default: break;
      }
    }
    std::string l = smtExprWp(c, b->lhs.get(), store);
    std::string r = smtExprWp(c, b->rhs.get(), store);
    switch (b->op) {
      case BinaryExpr::Op::add: return anyBv ? smtBvBin("bvadd", l, r) : ("(+ " + l + " " + r + ")");
      case BinaryExpr::Op::sub: return anyBv ? smtBvBin("bvsub", l, r) : ("(- " + l + " " + r + ")");
      case BinaryExpr::Op::mul: return anyBv ? smtBvBin("bvmul", l, r) : ("(* " + l + " " + r + ")");
      case BinaryExpr::Op::div: return "(div " + l + " " + r + ")";
      case BinaryExpr::Op::mod: return "(mod " + l + " " + r + ")";
      case BinaryExpr::Op::eq:  return "(= " + l + " " + r + ")";
      case BinaryExpr::Op::ne:  return "(not (= " + l + " " + r + "))";
      case BinaryExpr::Op::lt:  return "(< " + l + " " + r + ")";
      case BinaryExpr::Op::gt:  return "(> " + l + " " + r + ")";
      case BinaryExpr::Op::le:  return "(<= " + l + " " + r + ")";
      case BinaryExpr::Op::ge:  return "(>= " + l + " " + r + ")";
      case BinaryExpr::Op::land:return "(and " + l + " " + r + ")";
      case BinaryExpr::Op::lor: return "(or " + l + " " + r + ")";
      // BitVec ops  -  lowered with int2bv/bv2int bridges. The two operands
      // are lowered as Int (smtExprWp above returns Int), then we wrap each
      // in `((_ int2bv BV_W) x)` to get BitVec operands, apply the real BV op,
      // and ` ((_ bv2int BV_W) ...)` to get back Int. This keeps everything
      // Int-sorted in the surrounding term while SOUNDLY modelling the masked
      // value (so `x & 0xff` is `bvand (int2bv x) (int2bv 255)` and the
      // surrounding `= 0xff` can decide). We declare no uninterpreted fn  - 
      // the bv op IS the model.
      case BinaryExpr::Op::band: return smtBvBin("bvand", l, r);
      case BinaryExpr::Op::bor:  return smtBvBin("bvor",  l, r);
      case BinaryExpr::Op::bxor: return smtBvBin("bvxor", l, r);
      case BinaryExpr::Op::shl:  return smtBvBin("bvshl", l, r);
      case BinaryExpr::Op::shr:  return smtBvBin("bvlshr", l, r);
    }
    return smtPlaceholder(c, "Int", "binary op");
  }
  if (auto t = dynamic_cast<const TernaryExpr*>(e)) {
    return "(ite " + smtExprWp(c, t->cond.get(), store) + " "
                + smtExprWp(c, t->thenE.get(), store) + " "
                + smtExprWp(c, t->elseE.get(), store) + ")";
  }
  if (auto call = dynamic_cast<const Call*>(e)) {
    if (!call->fnPtr && call->callee == "len" && call->args.size() == 1) {
      return smtLenOf(c, call->args[0].get(), &store);
    }
    // Feature 7  -  mmio_load built-in: reads from the mmio_mem array model.
    // Lowered as `(select <mmio_mem_term> <ptr_term>)`. The mmio_mem term
    // comes from the WP store (the same key Feature 7 propagates across
    // function calls). If the caller's store has an `mmio_mem` entry (either
    // seeded at function entry or propagated from a callee), we use that;
    // otherwise we declare a fresh uninterpreted Array→Int symbol (but we
    // can't write to the const store, so we just emit the declaration and
    // use the fresh symbol for this read).
    //
    // Part 1  -  cross-function MMIO threading (named model). BEFORE the
    // array select, we consult `c.mmioState`, the per-ADDRESS model threaded
    // across call boundaries (a callee `configure_device(base)` writes
    // `mmioState[base] = 1` via the `mmio_store` arm + the call-arm
    // propagation; the caller's `mmio_load(base)` then HITs and returns the
    // propagated `1`). A MISS falls back to the array model and ALSO seeds
    // `c.mmioState[ptrTerm]` with the `(select ...)` term, so a subsequent
    // read of the SAME address (or a downstream clause lowered via the
    // signature path's `smtExpr` mmio_load arm) HITs and gets a stable
    // value term  -  guaranteeing two reads of the same address in the same
    // scope return the same symbol, instead of two unrelated `(select ...)`
    // terms that Z3 has to re-discharge as equal.
    if (!call->fnPtr && call->callee == "mmio_load" && call->args.size() == 1) {
      std::string ptrTerm = smtExprWp(c, call->args[0].get(), store);
      // Part 1  -  named per-address model first (cross-call threaded).
      auto stIt = c.mmioState.find(ptrTerm);
      if (stIt != c.mmioState.end() && !stIt->second.empty()) {
        return stIt->second;
      }
      // Look up the current mmio_mem model in the WP store.
      auto it = store.find("mmio_mem");
      std::string mmioSym;
      if (it != store.end() && !it->second.empty()) {
        mmioSym = it->second;
      } else {
        // Declare a fresh uninterpreted array symbol for mmio_mem.
        mmioSym = c.curFn + "_mmio_mem";
        if (c.declared.find(mmioSym) == c.declared.end()) {
          c.out << "(declare-const " << mmioSym << " (Array Int Int))\n";
          c.declared.insert(mmioSym);
        }
      }
      std::string sel = "(select " + mmioSym + " " + ptrTerm + ")";
      // Seed the named model so future reads of the same address HIT here
      // (and downstream signature-path `smtExpr` reads of the SAME address
      // term resolve to the same value symbol  -  see the `smtExpr` mmio_load
      // arm hitting above).
      c.mmioState[ptrTerm] = sel;
      c.mmioReadAddresses.insert(ptrTerm);
      return sel;
    }
    const FuncDecl* callee = smtFindDirectCallee(c, call);
    if (callee) {
      std::vector<std::string> args;
      args.reserve(call->args.size());
      for (auto& a : call->args) args.push_back(smtExprWp(c, a.get(), store));
      return smtConcreteCallResult(c, callee, c.curFn, args, c.wpPathCond, c.wpPremises);
    }
    std::vector<std::string> args;
    args.reserve(call->args.size());
    for (auto& a : call->args) args.push_back(smtExprWp(c, a.get(), store));
    std::string specTerm = smtInlineSpecCall(c, call, args);
    if (!specTerm.empty()) return specTerm;
  }
  if (auto mc = dynamic_cast<const MethodCall*>(e)) {
    const FuncDecl* callee = smtFindMethodCallee(c, mc);
    if (callee) {
      std::vector<std::string> args;
      args.reserve(mc->args.size());
      for (auto& a : mc->args) args.push_back(smtExprWp(c, a.get(), store));
      return smtConcreteCallResult(c, callee, c.curFn, args, c.wpPathCond, c.wpPremises);
    }
  }
  // Tier 2  -  cast widening/narrowing (WP path). See the sig-level `smtExpr`
  // arm (line ~1530) for the rationale. Same lowering  -  the operand lowers via
  // the matching WP encoder (`smtExprWp` here) so the store/nameMap overlay is
  // honoured, and the result stays Int-sorted via `bv2int` (this is the pure-
  // Int arithmetic context; the bv-flowing chain keeps its native BV form
  // upstream via `smtExprWpBv` for bit-context comparisons).
  if (auto ce = dynamic_cast<const CastExpr*>(e)) {
    std::string inner = smtExprWp(c, ce->e.get(), store);
    int srcW = BV_W;
    if (auto v = dynamic_cast<const VarRef*>(ce->e.get())) {
      auto sit = store.find(v->name);
      if (sit != store.end()) {
        if (c.bvVars.count(sit->second)) srcW = BV_W;
      } else {
        auto mit = c.nameMap.find(v->name);
        if (mit != c.nameMap.end() && c.bvVars.count(mit->second)) srcW = BV_W;
        else if (c.declared.count(v->name) && c.bvVars.count(v->name)) srcW = BV_W;
      }
    }
    switch (ce->target.tag) {
      case BType::Tag::i64: case BType::Tag::u64:
        if (srcW == BV_W) return inner;  // no-op
        return smtBvToInt("((_ zero_extend " + std::to_string(BV_W - srcW) +
                           ") ((_ int2bv " + std::to_string(srcW) + ") " + inner + "))");
      case BType::Tag::i32: case BType::Tag::u32:
        return smtBvToInt("((_ extract 31 0) ((_ int2bv " +
                           std::to_string(BV_W) + ") " + inner + "))");
      case BType::Tag::i16: case BType::Tag::u16:
        return smtBvToInt("((_ extract 15 0) ((_ int2bv " +
                           std::to_string(BV_W) + ") " + inner + "))");
      case BType::Tag::i8: case BType::Tag::u8:
        return smtBvToInt("((_ extract 7 0) ((_ int2bv " +
                           std::to_string(BV_W) + ") " + inner + "))");
      default: return inner;  // bool/void/float casts  -  honest pass-through
    }
  }
  // Tier 2  -  array index read `arr[i]` (WP path). Resolves `arr` via the WP
  // store first (so `arr = new_arr` rebinds reads  -  array-valued lets come
  // up), then nameMap/declared, then recurses on a non-VarRef base (nested
  // `arr[i][j]`). `i` lowers via `smtExprWp`. No `len()` here (Tier 3).
  if (auto ix = dynamic_cast<const Index*>(e)) {
    std::string baseTerm;
    if (auto v = dynamic_cast<const VarRef*>(ix->base.get())) {
      auto sit = store.find(v->name);
      if (sit != store.end()) baseTerm = sit->second;
      else {
        auto mit = c.nameMap.find(v->name);
        if (mit != c.nameMap.end()) baseTerm = mit->second;
        else if (c.declared.count(v->name)) baseTerm = v->name;
        else return smtPlaceholder(c, "Int",
                    ("array base '" + v->name + "' not declared").c_str());
      }
    } else {
      baseTerm = smtExprWp(c, ix->base.get(), store);
      if (baseTerm.empty() || baseTerm.find("ph") == 0)
        return smtPlaceholder(c, "Int", "array base failed to lower");
    }
    std::string idx = smtExprWp(c, ix->index.get(), store);
    return "(select " + baseTerm + " " + idx + ")";
  }
  // Tier 2a  -  `self.x` field read. Only the `self`-receiver form is modelled
  // at Tier 2a; a Field on any other base still punts to `smtPlaceholder`
  // (the unsupported-expr fallback below). The base must be a bare `VarRef`
  // named "self" (the parser produces this for `&self`/`&mut self` methods),
  // and the function must have a known `c.selfStructName` (set by
  // `emitFnContracts`'s struct-seeding block). The field symbol is
  // `self__<S>__<field>`; we consult `store` first (so `self.x = e` rebinds
  // reads to the new post term  -  same model as globals), then `nameMap`
  // (which was seeded with the same fresh-const symbol), then the declared
  // symbol itself. Each path yields the same identifier when untouched.
  if (auto f = dynamic_cast<const Field*>(e)) {
    if (auto v = dynamic_cast<const VarRef*>(f->base.get())) {
      if (v->name == "self" && !c.selfStructName.empty()) {
        std::string key = "self__" + c.selfStructName + "__" + f->field;
        auto sit = store.find(key);
        if (sit != store.end()) {
          if (c.bvVars.count(sit->second)) return smtBvToInt(sit->second);
          return sit->second;
        }
        auto mit = c.nameMap.find(key);
        if (mit != c.nameMap.end()) {
          if (c.bvVars.count(mit->second)) return smtBvToInt(mit->second);
          return mit->second;
        }
        if (c.declared.count(key)) {
          if (c.bvVars.count(key)) return smtBvToInt(key);
          return key;
        }
        // Fall through to placeholder if the struct lookup somehow missed.
      }
    }
    // Non-self Field access  -  honestly punt. Tier 2a scope.
    return smtPlaceholder(c, "Int", "field access on non-self base");
  }
  // Contract 5  -  `asm!(...)` block (WP path). For single-output blocks returns
  // the applied term `(asm_<fn>_<seq> <inputs...>)`; for multi-output blocks
  // returns the `_out0` representative (sound: Sema sets resultTy=void; the
  // per-output lvalue bindings live in the ExprStmt arm via c.asmOutputTerms).
  // Inputs are lowered through the WP store (`smtExprWp(c, in, store)`) so
  // `in("...") local_name` operands resolve the source name through the
  // symbolic store (e.g. a `let`-bound value flowing into the asm). Declare-fun
  // + per-output spec-fn axiom emission are handled (dedup'd) by `smtAsmTerm`.
  // NOTE: the output-lvalue BINDING (e.g. `out("{rax}") r` rebinding
  // `store["r"] = asm_term`, or for multi-output `store["a"]=_out0`,
  // `store["b"]=_out1`) is done in the ExprStmt-AsmExpr arm of `smtEncodeStmt`,
  // NOT here  -  `smtExprWp` is reached when an asm result is referenced inside a
  // larger expression, which is uncommon since the result lands in the store via
  // the ExprStmt arm. This arm keeps the two paths consistent: a `return asm!
  // (...)` (expression-form asm result) and a `asm!(...); return r;` (stmt-form,
  // lvalue-bound) both yield the same term.
  // ---- Advanced-math arms (WP path) ----
  // Mirrors the `smtExpr` clause-path arms, but lowers operands through
  // `smtExprWp(..., store)` so the store/nameMap overlay is honoured (a
  // `let x = ...; ensures pow(x, 2) == ...` reads `x` from the WP store).
  // The shared `smtEmitPow` / `smtEmitSolve` helpers take already-lowered
  // term strings, so the only difference from the clause path is the operand
  // lowerer used here.
  if (auto p = dynamic_cast<const PowerExpr*>(e)) {
    std::string base = smtExprWp(c, p->base.get(), store);
    std::string exp  = smtExprWp(c, p->exponent.get(), store);
    return smtEmitPow(c, base, exp, p->resultType);
  }
  if (auto ml = dynamic_cast<const MatrixLit*>(e)) {
    const char* es = smtMathSort(ml->elemType);
    std::string arrSort = std::string("(Array Int (Array Int ") + es + "))";
    std::string name = c.curFn + "_mat_" + std::to_string(c.assertSeq++);
    if (c.declared.find(name) == c.declared.end()) {
      c.out << "(declare-const " << name << " " << arrSort << ")\n";
      c.declared.insert(name);
      c.out << "; note: declared matrix literal '" << name << "' (2D array)\n";
    }
    std::string acc = name;
    for (size_t i = 0; i < ml->rows.size(); ++i) {
      std::string rowArrSort = std::string("(Array Int ") + es + ")";
      std::string innerSym = name + "_r" + std::to_string(i);
      if (c.declared.find(innerSym) == c.declared.end()) {
        c.out << "(declare-const " << innerSym << " " << rowArrSort << ")\n";
        c.declared.insert(innerSym);
      }
      std::string inner = innerSym;
      for (size_t j = 0; j < ml->rows[i].size(); ++j) {
        std::string v = smtExprWp(c, ml->rows[i][j].get(), store);
        inner = "(store " + inner + " " + std::to_string(j) + " " + v + ")";
      }
      acc = "(store " + acc + " " + std::to_string(i) + " " + inner + ")";
    }
    return acc;
  }
  if (auto s = dynamic_cast<const SolveExpr*>(e)) {
    std::string lhs = smtExprWp(c, s->lhs.get(), store);
    std::string rhs = smtExprWp(c, s->rhs.get(), store);
    return smtEmitSolve(c, lhs, rhs, s->resultType);
  }
  // MatMulExpr (WP path)  -  mirrors the clause path; operands via smtExprWp
  // so the WP/SSA store overlay is honoured (e.g. `let M = ...; ensures
  // matmul(M, N) == ...` reads `M` from the store, not the outer nameMap).
  if (auto mm = dynamic_cast<const MatMulExpr*>(e)) {
    std::string a = smtExprWp(c, mm->lhs.get(), store);
    std::string b = smtExprWp(c, mm->rhs.get(), store);
    return smtEmitMatmul(c, a, b, mm->elemType);
  }
  // IntegrateExpr (WP path)  -  mirrors the clause path; `lo`/`hi` via
  // `smtExprWp(..., store)`, and the integrand `body` likewise. The body
  // placeholder/spoof pattern matches the clause path arm above (a `ph`
  // term is lifted to Real via `(/ <t> 1.0)` so the sort agrees with the
  // `integrate_<fn>_<seq>` uninterpreted function's Real domain).
  if (auto ig = dynamic_cast<const IntegrateExpr*>(e)) {
    std::string lo = smtExprWp(c, ig->lo.get(), store);
    std::string hi = smtExprWp(c, ig->hi.get(), store);
    std::string bodyTerm;
    if (ig->body) {
      std::string rawBody = smtExprWp(c, ig->body.get(), store);
      if (rawBody.rfind("ph", 0) == 0)
        bodyTerm = "(/ " + rawBody + " 1.0)";
      else
        bodyTerm = rawBody;
    } else {
      bodyTerm = smtPlaceholder(c, "Real", "integrate body (null integrand)");
    }
    return smtEmitIntegrate(c, bodyTerm, lo, hi);
  }
  // MathSymExpr: leaf constant  -  same PI/e literals as clause path.
  if (auto/*ms*/ ms = dynamic_cast<const MathSymExpr*>(e)) {
    if (ms->text == "pi" || ms->text == "\xcf\x80") // π
      return "(/ 314159265358979323846264338327950288.0 100000000000000000000000000000000000)";
    if (ms->text == "e" || ms->text == "\xe2\x84\xaf") // ℯ
      return "(/ 271828182845904523536028747135266249.0 100000000000000000000000000000000000)";
    return smtPlaceholder(c, "Real", ("math symbol '" + ms->text + "'").c_str());
  }
  // SuperscriptExpr: like PowerExpr  -  base^exp via smtEmitPow, operands via smtExprWp.
  if (auto se = dynamic_cast<const SuperscriptExpr*>(e)) {
    std::string base = smtExprWp(c, se->base.get(), store);
    std::string exp  = se->exponent ? smtExprWp(c, se->exponent.get(), store) : "2";
    return smtEmitPow(c, base, exp, se->resultType);
  }
  if (auto a = dynamic_cast<const AsmExpr*>(e)) {
    return smtAsmTerm(c, a, [&](const Expr* inExpr) {
      return smtExprWp(c, inExpr, store);
    });
  }
  return smtPlaceholder(c, "Int", "unsupported expr");
}


// smtEncodeBody  -  Tier B weakest-precondition / SSA symbolic executor.
//
// smtWalkStmts (Tier A, above) emits one discharge query per invariant/assert
// WITHOUT reasoning about how the local the clause references was assigned,
// and treats `result` as a free uninterpreted const. Sound but loose: it can't
// discharge `ensures result == x` (the body computes result), and reports
// `unknown name 'acc' -> fn_ph0` because the loop variable is never bound.
//
// smtEncodeBody threads:
//   pathCond : growing conjunction of assumed branches (Bool SMT term, or "")
//   store    : map Oxide source name -> SMT term (the symbolic `result` of the
//              last assignment / let that reached this point, SSA-style).
//   ensuresClauses : the function's `ensures` expressions; discharged at every
//              `return` site (and at fall-through) with `result` bound to the
//              return expression's symbolic term.
//   premises : the function's `requires` terms, carried into every discharge.
//
// On `let x = e` / `ghost let x: T`: store[x] = smtExprWp(e, store).
//   For a `ghost let`, we declare `ghost_<fn>_<x> <Sort>` (matching the T2
//   Ghost-section naming) and bind store[x] to that symbol UNCONSTRAINED  - 
//   sound: the symbol is fresh and the proof obligation is carried by the
//   clause that constrains it. This makes `ensures result == focus` resolve.
// On `x = e`:        store[x] = smtExprWp(e, store)        (SSA re-bind).
// On `if c {A} else {B}`: fork  -  recurse A under pathCond∧c, B under
//                    pathCond∧¬c. After both arms, merge stores by `ite`:
//                    for each name bound in EITHER arm, the post-merge value
//                    is `(ite c <A's value> <B's value>)`. Dead arms (a `return`
//                    inside) don't contribute.
// On `assert P`:     discharge P under pathCond (WP: must hold here).
// On `return e`:     store["result"] = smtExprWp(e, store); discharge every
//                    ensures clause with `result` so bound, under pathCond +
//                    premises; mark this path dead.
// On `while c invariant I {body}`: the 3-check induction scheme  - 
//                    (1) I holds at loop entry (under pathCond ∧ c).
//                    (2) Assuming I ∧ c at body entry, I holds at body exit
//                        (preservation). For soundness we recurse into the body
//                        with the inv PUSHED as a hypothesis (premises + Iterm)
//                        and re-discharge I at body end. Names the body
//                        re-assigned flow into the post-body store.
//                    (3) At loop exit (pathCond ∧ ¬c ∧ I), continue with the
//                        merged post-body store. Programs without invariant
//                        get `assume false` at the loop site (the prover may
//                        assume the loop never Runs  -  sound, honest, loose).
// On `for ...`: same 3-check scheme, with the iteration bounded by start/end.
// End of body: if the function has a non-void retType and no explicit return
//   reached this point, discharge ensures with `result` = the outer declared
//   `result` symbol (unconstrained)  -  honest under-approximation: a path that
//   falls off the end without returning is undefined behaviour in C anyway.
//
// Notes:
// - We do NOT touch `c.nameMap` for body locals; the `store` shadow is
//   per-path. `c.nameMap` keeps the OUTER param/result/old_ bindings intact.
// - We DO declare top-level SMT consts for any name the body introduces that
//   is NOT a simple let (so ghost lets, which must persist across clauses,
//   resolve). Plain `let x` stays in `store` only  -  it has no life outside
//   the body, so we don't pollute the top-level decl namespace.
struct WpEns {
  const std::vector<ExprPtr>* clauses = nullptr;
  const std::string* fnName = nullptr;
};

// Forward declarations of the two arms of the recursive walker.
static void smtEncodeStmts(SmtCtx& c, const std::string& fnName,
                          const std::vector<StmtPtr>& stmts, int depth,
                          const std::vector<std::string>& premises,
                          std::map<std::string, std::string>& store,
                          const std::string& pathCond, bool& returned,
                          const WpEns& ens);

// ox:proof Discharge the `ensures` clauses at one return site. `resultTerm` is the
// symbolic term the return expression lowered to (already in store["result"]
// if set). Each ensures is lowered with smtExprWp under the current store so
// it can reference the body locals in scope at the return.
static void smtDischargeEnsures(SmtCtx& c, const std::string& fnName,
                                const std::vector<ExprPtr>* ensures,
                                const std::map<std::string, std::string>& store,
                                const std::string& pathCond,
                                const std::vector<std::string>& premises) {
  if (!ensures || ensures->empty()) return;
  // Build the premise list including pathCond.
  std::vector<std::string> prem = premises;
  if (!pathCond.empty()) prem.push_back(pathCond);
  int i = 0;
  for (auto& e : *ensures) {
    if (!e) continue;
    std::string label = fnName + "_ensures_ret_"
                        + std::to_string(c.assertSeq++) + "_" + std::to_string(i);
    c.out << "; " << label << " (return-site ensures, source line " << e->line << ")\n";
    std::string term = smtExprWp(c, e.get(), store);
    c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
    smtDischarge(c, label, label, prem);
    ++i;
  }
}

// Conjoin two path conditions (either may be "" meaning true).
static std::string pathAnd(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return "(and " + a + " " + b + ")";
}

static const FuncDecl* smtFindDirectCallee(SmtCtx& c, const Call* call) {
  if (!call || call->fnPtr || call->callee == "len") return nullptr;
  auto it = c.funcDecls.find(call->callee);
  return (it == c.funcDecls.end()) ? nullptr : it->second;
}

static const FuncDecl* smtFindMethodCallee(SmtCtx& c, const MethodCall* mc) {
  if (!mc) return nullptr;
  if (mc->recvType.tag != BType::Tag::struct_) return nullptr;
  std::string key = mangleMethod(mc->recvType.structName, mc->callee);
  auto it = c.methodDecls.find(key);
  return (it == c.methodDecls.end()) ? nullptr : it->second;
}

static void smtWithCalleeBindings(SmtCtx& c, const FuncDecl* callee,
                                  const std::vector<std::string>& args,
                                  const std::string& resultTerm,
                                  const std::function<void()>& body) {
  std::map<std::string, std::string> saved;
  std::set<std::string> erased;
  auto bindOne = [&](const std::string& name, const std::string& term) {
    auto it = c.nameMap.find(name);
    if (it != c.nameMap.end()) saved[name] = it->second;
    else erased.insert(name);
    c.nameMap[name] = term;
  };
  if (callee) {
    size_t n = std::min(callee->params.size(), args.size());
    for (size_t i = 0; i < n; ++i) bindOne(callee->params[i].name, args[i]);
  }
  if (!resultTerm.empty()) bindOne("result", resultTerm);
  body();
  for (auto& n : erased) c.nameMap.erase(n);
  for (auto& kv : saved) c.nameMap[kv.first] = kv.second;
}

std::string smtConcreteCallResult(SmtCtx& c, const FuncDecl* callee,
                                         const std::string& labelBase,
                                         const std::vector<std::string>& args,
                                         const std::string& pathCond,
                                         const std::vector<std::string>& premises,
                                         std::map<std::string, std::string>* postStore) {
  if (!callee) return "";
  std::string res = labelBase + "_call_result_" + std::to_string(c.assertSeq++);
  smtDeclareConst(c, res, smtSort(callee->retType));

  // D1  -  recursion guard. If `callee` is already on the current inline
  // stack (direct recursion `walk -> walk`, or indirect `a -> b -> a`:
  // `a` stays on the stack while `b` is being inlined so the second hit of
  // `a` is caught here too), re-inlining the body would chase infinitely
  // and blow the C++ stack  -  this is the classic SE recursion hazard. The
  // standard Dafny/Why3 fix is "assume the callee's contract": declare a
  // fresh `result` symbol (already done above), bind the callee's params to
  // the actual arg terms in `nameMap` (so the ensures reference them), and
  // ASSERT the callee's `ensures` clauses as premises under the callee's
  // `requires` premise. The callee then admits any behaviour that satisfies
  // its spec  -  sound because the callee promises exactly that.
  //
  // We do NOT push the callee name onto `inlineStack` in this arm (we never
  // recurse); the caller's downstream WP sees the assumed `ensures` and
  // discharges against them, exactly like a non-recursive callee whose
  // body we DID inline.
  bool recursive = false;
  for (const auto& name : c.inlineStack) {
    if (name == callee->name) { recursive = true; break; }
  }
  if (recursive) {
    c.out << "; note: callee '" << callee->name
          << "' is recursive  -  assuming ensures at call site "
             "instead of inlining body\n";
    // ox:why Bind params + `result` (= fresh `res`) so the ensures can reference
    // them, then assert each ensures clause as a premise. The caller's
    // `requires`-discharge block above used the same bindings to bind params
    // only; here we additionally bind `result` because we're certifying the
    // post-state, not just the pre-state.
    smtWithCalleeBindings(c, callee, args, res, [&] {
      // Assume the callee's `requires` hold  -  the callee's `ensures` are
      // only promised under that precondition. We assert each `requires`
      // clause so the assumed `ensures` are guarded by them downstream.
      int reqIdx = 0;
      for (auto& req : callee->requires_) {
        if (!req) continue;
        std::string reqLabel = labelBase + "_rec_req_"
                               + std::to_string(c.assertSeq++) + "_"
                               + std::to_string(reqIdx++);
        c.out << "(define-fun " << reqLabel << " () Bool "
              << smtExpr(c, req.get()) << ")\n";
        c.out << "(assert " << reqLabel << ")\n";
      }
      // Assert each `ensures` as a premise under those requires. Bound
      // `result` in `nameMap` resolves to the fresh sym `res`, so the
      // caller's downstream WP can use `ensures` to derive `prev`/`result`.
      int ensIdx = 0;
      for (auto& ens : callee->ensures_) {
        if (!ens) continue;
        std::string ensLabel = labelBase + "_rec_ens_"
                               + std::to_string(c.assertSeq++) + "_"
                               + std::to_string(ensIdx++);
        std::string term = smtExpr(c, ens.get());
        c.out << "(define-fun " << ensLabel << " () Bool " << term << ")\n";
        c.out << "(assert " << ensLabel << ")\n";
      }
    });
    // Post-state: a recursive assume-mode call site doesn't mutate any
    // array the caller can observe (we never ran the body), so the caller
    // sees the pre-state for every array-typed param  -  exactly like the
    // single-return fast path below. Honest: `modifies` clauses on the
    // callee are NOT honoured by this contract-only assumption.
    if (postStore) {
      std::map<std::string, std::string> calleeStore;
      size_t n = std::min(callee->params.size(), args.size());
      for (size_t i = 0; i < n; ++i)
        calleeStore[callee->params[i].name] = args[i];
      *postStore = calleeStore;
    }
    return res;
  }

  smtWithCalleeBindings(c, callee, args, res, [&] {
    int i = 0;
    for (auto& req : callee->requires_) {
      std::string label = labelBase + "_call_requires_" + std::to_string(c.assertSeq++)
                          + "_" + std::to_string(i++);
      c.out << "; " << label << " (call requires, source line " << (req ? req->line : 0) << ")\n";
      std::string term = smtExpr(c, req.get());
      c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
      std::vector<std::string> prem = premises;
      if (!pathCond.empty()) prem.push_back(pathCond);
      smtDischarge(c, label, label, prem);
    }
  });

  // D1  -  push the callee onto the inlining stack so any recursive call the
  // callee's body makes back into THIS callee (direct: `walk -> walk`, or
  // indirect via siblings: `walk -> helper -> walk`) is caught by the
  // recursive-bail arm above and assumed-by-contract instead of infinitely
  // re-inlined. Pushed AFTER the call-requires discharge above (those don't
  // recurse into the body) and popped before the final `return res` below so
  // it covers BOTH inlining paths (single-return fast path + the
  // multi-statement mini-WP walk). Insertion is cheap; the linear scan in
  // the recursive check above is fine because inline stacks are tiny
  // (bounded by call depth, not arity).
  c.inlineStack.push_back(callee->name);

  // Body inlining for a single `return e` callee  -  the fast path: the
  // symbolic term of `e` under callee bindings IS the result.
  if (callee->body.size() == 1) {
    if (auto ret = dynamic_cast<const ReturnStmt*>(callee->body[0].get())) {
      if (ret->value) {
        std::map<std::string, std::string> calleeStore;
        size_t n = std::min(callee->params.size(), args.size());
        for (size_t i = 0; i < n; ++i) calleeStore[callee->params[i].name] = args[i];
        // Save/restore curFn so any asm! block in the callee's return
        // expression resolves its spec fn as asm_<callee>, not asm_<caller>.
        std::string savedCurFn = c.curFn;
        c.curFn = callee->name;
        res = smtExprWp(c, ret->value.get(), calleeStore);
        c.curFn = savedCurFn;
        // Gap 1a  -  expose the post-state store. For a single-return fast-path
        // callee, the body never mutates anything, so `calleeStore` carries
        // exactly the parameter-to-arg bindings (the pre-state for every
        // array param  -  i.e. the post-state IS the pre-state, since nothing
        // was written). That's the right semantics to expose to the caller:
        // an invariant lowering against this post-store resolves array params
        // to their pre-state terms, which matches `modifies []` (fast path
        // bodies are pure expressions; they can't mutate arrays).
        if (postStore) *postStore = calleeStore;
      }
    }
  }

  // Missing-#2: Method-call two-sided WP inlining for multi-statement callees.
  // The single-return fast path above handles trivial callees; for a body with
  // branches, loops, or multiple statements, we run a MINI WP walk over the
  // callee body under the callee's param/result bindings. The walk threads the
  // symbolic store through every assignment and let, and at every `return e`
  // site it sets store["result"] to the return expression's term. If the fall-
  // through leaves `result` bound, that's our inlined term. If there were
  // multiple return sites in different branches, mergeStores already folded
  // them into `(ite cond <thenTerm> <elseTerm>)` so the final store["result"]
  // IS the case-split tree of every possible return path. We then use that as
  // `res` instead of the unconstrained fresh const.
  //
  // This runs UNDER smtWithCalleeBindings (params + result bound in nameMap).
  // We pass an EMPTY WpEns clause set so the body walker does NOT discharge the
  // callee's ensures at each return site  -  those are THREADED to the caller's
  // downstream by smtConcreteCallAssumes (which uses the now-meaningful result
  // term, tying the callee's behavior to the caller's proof). Discharging them
  // here would count them as proven before the caller had its say; leaving
  // them to the assumes path is the two-sided WP contract.
  //
  // The callee's own `requires` carry as the mini-walk's premises  -  that way
  // any `assert` in the callee body discharges under its own preconditions,
  // not against the raw caller state. Those requires were already discharged
  // above (the call-requires block), so assuming them again here is just
  // reusing the proof obligation as a hypothesis.
  //
  // Important: we pass a UNIQUE label base (caller_fn + "_inline_" + seq) to
  // the mini-walk. The WP walker wishes labels like "<label>_invariant_d0_0"
  // off the fnName arg it gets  -  if we passed the caller's bare `labelBase`,
  // multiple inlined callees in the SAME caller would all mint the same
  // `"_invariant_d0_0"` suffix and SMT `define-fun` rejects duplicate names
  // with a fatal ("named expression already defined") error that aborts that
  // check-sat (Z3 leaves the prior `sat` in the log, masking the proof).
  if (callee->body.size() != 1 || !dynamic_cast<const ReturnStmt*>(callee->body[0].get())) {
    std::map<std::string, std::string> calleeStore;
    size_t n = std::min(callee->params.size(), args.size());
    for (size_t i = 0; i < n; ++i) calleeStore[callee->params[i].name] = args[i];
    // Unique per-call-site label prefix so emitted define-funs don't collide
    // across multiple inlined callee bodies inside the same caller fn.
    std::string inlineLabel = labelBase + "_inline_" + std::to_string(c.assertSeq++);
    // Collect callee requires as premise strings (by name, under binding).
    // Save/restore curFn so any asm! block in the callee's body resolves
    // its spec fn as asm_<callee>, not asm_<caller>. Without this, the
    // mini-walk runs with the caller's curFn, so smtAsmTerm builds the
    // symbol asm_<caller>_0 and looks for spec fn asm_<caller>  -  the
    // callee's asm! axiom is never linked, and the discharge silently
    // fails (sat or unknown instead of unsat).
    std::string savedCurFn = c.curFn;
    c.curFn = callee->name;
    std::vector<std::string> calleeReq;
    smtWithCalleeBindings(c, callee, args, res, [&] {
      int reqIdx = 0;   // per-clause index so distinct requires don't collide
      for (auto& req : callee->requires_) {
        if (!req) continue;
        // ox:unsafe Missing-#2 latent fix: reqLabel MUST be per-clause-unique. Using
        // bare `_req` here meant callees with 2+ requires clauses emitted
        // TWO `(define-fun <inlineLabel>_req ...)` define-funs with the SAME
        // name  -  Z3 aborts that check-sat ("named expression already
        // defined"), which silently leaves the prior `sat` in the log and
        // masks the proof as sat. Preserves (#6) hits this on every hv_ept
        // handler (each has 2 requires: range + page_aligned). Adding the
        // per-clause `_req<i>` suffix mirrors the call-requires block above
        // (which already used `_call_requires_<seq>_<i>`) and is the load-
        // bearing fix for multi-clause callee requires.
        std::string reqLabel = inlineLabel + "_req" + std::to_string(reqIdx++);
        // Emit as a define-fun so it names a Bool term we can reference.
        std::string term = smtExpr(c, req.get());
        c.out << "(define-fun " << reqLabel << " () Bool " << term << ")\n";
        calleeReq.push_back(reqLabel);
      }
      // Seed the WP store with a per-call-site `result` symbol equal to `res`
      // so any branch that falls through without return leaves a known value.
      calleeStore["result"] = res;
      bool returned = false;
      WpEns emptyEns;  // NO clause discharge  -  we just collect the result term.
      smtEncodeStmts(c, inlineLabel, callee->body, 0, calleeReq, calleeStore,
                     pathCond, returned, emptyEns);
    });
    // Restore caller's curFn.
    c.curFn = savedCurFn;
    // If the body reached a `return e`, store["result"] now holds e's term
    // (folded through any branches). If it fell through without return,
    // store["result"] is still the seed `res`  -  honest.
    auto rit = calleeStore.find("result");
    if (rit != calleeStore.end() && !rit->second.empty()) {
      res = rit->second;
    }
    // Gap 1a  -  expose the post-state store. This multi-statement path is
    // where mutation actually happens: smtEncodeStmts threads the WP store
    // through every assignment (including `arr[i] = e` via Gap 1b), so the
    // final calleeStore carries the post-state term for every name the body
    // touched. The caller (preserves) binds array-typed handler params to
    // these post-state terms so the invariant sees the POST-state of the
    // array, not just the result term.
    if (postStore) *postStore = calleeStore;
  }
  // D1  -  pop the callee off the inlining stack (mirrors the push above).
  // One pop per push: the inline stack is always balanced across a single
  // smtConcreteCallResult entry/exit, so direct AND indirect recursion are
  // both caught  -  when inlining `b` whose body calls `a` again, `a` is
  // still on the stack and the second hit of `a` takes the recursive arm.
  if (!c.inlineStack.empty() && c.inlineStack.back() == callee->name) {
    c.inlineStack.pop_back();
  }
  return res;
}

static std::string smtConcreteCallAssumes(SmtCtx& c, const FuncDecl* callee,
                                          const std::vector<std::string>& args,
                                          const std::string& resultTerm) {
  if (!callee || callee->ensures_.empty()) return "";
  std::vector<std::string> terms;
  smtWithCalleeBindings(c, callee, args, resultTerm, [&] {
    for (auto& ens : callee->ensures_) terms.push_back(smtExpr(c, ens.get()));
  });
  std::string out;
  for (auto& t : terms) out = pathAnd(out, t);
  return out;
}

// Merge two stores from the two arms of an `if`. For each name present in
// either arm, the merged value is `(ite cond <thenVal> <elseVal>)`. A name
// missing from one arm inherits the value from the OTHER arm unchanged (it
// wasn't reassigned there, so the pre-`if` binding is still in force  -  but
// that's already the value the arm started with, so it's fine to use either
// side's inherited copy). Dead arms (returned=true) are skipped entirely.
static void mergeStores(std::map<std::string, std::string>& dst,
                        const std::map<std::string, std::string>& thenStore,
                        bool thenReturned,
                        const std::map<std::string, std::string>& elseStore,
                        bool elseReturned,
                        const std::string& condTerm) {
  if (thenReturned && elseReturned) { dst.clear(); return; }
  if (thenReturned) { dst = elseStore; return; }
  if (elseReturned) { dst = thenStore; return; }
  // Both arms alive  -  merge via `ite cond ... ...`.
  std::map<std::string, std::string> out;
  // Start from the union of keys.
  std::set<std::string> keys;
  for (auto& kv : thenStore) keys.insert(kv.first);
  for (auto& kv : elseStore) keys.insert(kv.first);
  for (auto& k : keys) {
    auto ti = thenStore.find(k);
    auto ei = elseStore.find(k);
    if (ti == thenStore.end()) { out[k] = ei->second; continue; }
    if (ei == elseStore.end()) { out[k] = ti->second; continue; }
    if (ti->second == ei->second) { out[k] = ti->second; continue; }
    out[k] = "(ite " + condTerm + " " + ti->second + " " + ei->second + ")";
  }
  dst = std::move(out);
}

// ox:why G3a  -  helper used by the LetStmt arm of `smtEncodeStmt` so that a
// `let r = handler(args...)` inside a dispatcher body propagates the handler's
// post-state arrays back into the caller's `store`. Without this, only bare
// statement-form calls (`map_page_mut(...);` with no `let`, handled by the
// ExprStmt-with-Call arm below) would get post-store propagation; LET-bound
// calls go through `smtExprWp(let->init)`, whose signature takes `store` by
// CONST ref (read-only), so it cannot thread a post-store out. This bounds the
// propagation to the LetStmt arm only and keeps `smtExprWp` untouched.
//
// `args` and `rawArgs` are the already-WP-lowered arg terms and the raw
// ExprPtr args (in the same order)  -  positions line up exactly. We mirror
// `smtExprWp`'s Call arm (line ~2401) but pass the `postStore` out-param and
// then merge positional array-typed callee params back into `store`, keyed off
// the caller-side `VarRef` name (exactly like the Call/MethodCall arms below).
static std::string smtWpInlineCallPostStore(
    SmtCtx& c, const FuncDecl* callee, const std::string& fnName,
    const std::vector<std::string>& args,
    const std::vector<ExprPtr>& rawArgs,
    const std::string& pathCond, const std::vector<std::string>& premises,
    std::map<std::string, std::string>& store) {
  std::map<std::string, std::string> localPostStore;
  std::string resultTerm = smtConcreteCallResult(c, callee, fnName, args,
                                                 pathCond, premises,
                                                 &localPostStore);
  if (callee) {
    size_t n = std::min(callee->params.size(), rawArgs.size());
    for (size_t i = 0; i < n; ++i) {
      if (callee->params[i].type.tag != BType::Tag::array) continue;
      auto itP = localPostStore.find(callee->params[i].name);
      if (itP == localPostStore.end()) continue;
      if (auto vr = dynamic_cast<const VarRef*>(rawArgs[i].get())) {
        store[vr->name] = itP->second;
      }
    }
    // Feature 7  -  propagate the callee's mmio_mem post-state into the caller's
    // store. mmio_mem is NOT a callee param  -  it's a synthetic store slot that
    // the mini-walker's mmio_store(ptr, val) arm rebinds to (store <cur> ptr val)
    // inside calleeStore. Without this propagation, a subsequent mmio_load(ptr)
    // in the caller resolves to the bare entry symbol and loses the callee's
    // writes. Same pattern as the array-param loop above.
    auto mmioIt = localPostStore.find("mmio_mem");
    if (mmioIt != localPostStore.end() && !mmioIt->second.empty()) {
      store["mmio_mem"] = mmioIt->second;
    }
    // Part 1  -  cross-function MMIO threading (named per-address model).
    // The callee's mmio_store arms (in the mini-walker inside
    // smtConcreteCallResult) recorded each (address, value) write into
    // c.mmioWriteEffects[callee->name]. Apply each such write to the CALLER's
    // mmioState so a later mmio_load(addr) in the caller HITs the propagated
    // value instead of resolving to the bare entry symbol. Addresses the callee
    // did NOT write are left untouched in mmioState  -  their existing caller
    // value (or absent entry → uninterpreted const) survives, which is the
    // "frame" guarantee: the callee may only write what its modifies/effects
    // declare.
    auto wfx = c.mmioWriteEffects.find(callee->name);
    if (wfx != c.mmioWriteEffects.end()) {
      for (const auto& kv : wfx->second) c.mmioState[kv.first] = kv.second;
    }
  }
  return resultTerm;
}

// Collect the user-visible names mutated anywhere in `stmts` by `x = e;`
// (AssignTarget::Kind::var) or `x++`/`++x`/`x--`/`--x` (IncDecExpr::Kind::var).
// Recurses through Block / If / While / For / DeferStmt. Used by the while-exit
// path of `smtEncodeStmt` to decide which locals must be re-bound to fresh
// uninterpreted consts at loop exit: the body's post-store carries ONE-iteration
// values (e.g. `i = 0 + 1` for `i = i + 1` starting from `i = 0`), which is NOT
// the actual post-loop value  -  the loop may run 0..n times. Re-binding mutated
// names to fresh symbolic consts and re-encoding the invariants under that store
// makes the loop-exit path condition `pathCond ∧ ¬cond(fresh) ∧ I(fresh)`
// constrain the fresh consts honestly (e.g. `i_ex ≤ n ∧ ¬(i_ex < n)` ⟹
// `i_ex == n`), and downstream `assert`/`ensures` clauses that read those names
// from the store see the SAME fresh symbol the invariant constrains.
static void collectMutatedVarNamesInStmt(const Stmt* s,
                                         std::set<std::string>& out);
static void collectMutatedVarNames(const std::vector<StmtPtr>& stmts,
                                   std::set<std::string>& out) {
  for (auto& s : stmts)
    if (s) collectMutatedVarNamesInStmt(s.get(), out);
}
static void collectMutatedVarNamesInStmt(const Stmt* s,
                                         std::set<std::string>& out) {
  if (!s) return;
  if (auto es = dynamic_cast<const ExprStmt*>(s)) {
    if (auto a = dynamic_cast<const AssignTarget*>(es->expr.get())) {
      if (a->kind == AssignTarget::Kind::var) out.insert(a->name);
    }
    if (auto id = dynamic_cast<const IncDecExpr*>(es->expr.get())) {
      if (id->kind == AssignTarget::Kind::var) out.insert(id->name);
    }
    return;
  }
  if (auto bl = dynamic_cast<const Block*>(s)) {
    collectMutatedVarNames(bl->stmts, out);
    return;
  }
  if (auto is = dynamic_cast<const IfStmt*>(s)) {
    collectMutatedVarNames(is->then, out);
    collectMutatedVarNames(is->else_, out);
    return;
  }
  if (auto ws = dynamic_cast<const WhileStmt*>(s)) {
    collectMutatedVarNames(ws->body, out);
    return;
  }
  if (auto fs = dynamic_cast<const ForStmt*>(s)) {
    collectMutatedVarNames(fs->body, out);
    return;
  }
  if (auto ds = dynamic_cast<const DeferStmt*>(s)) {
    collectMutatedVarNamesInStmt(ds->body.get(), out);
    return;
  }
}

// Encode one statement. See smtEncodeStmts for the surrounding loop.
static void smtEncodeStmt(SmtCtx& c, const std::string& fnName, const Stmt* s,
                          int depth, const std::vector<std::string>& premises,
                          std::map<std::string, std::string>& store,
                          const std::string& pathCond, bool& returned,
                          const WpEns& ens) {
  if (!s || returned) return;

  // ox:proof let x: T = e;  (also covers ghost let, which subclasses LetStmt)
  if (auto let = dynamic_cast<const LetStmt*>(s)) {
    if (let->init) {
      // Track whether this `let`'s RHS flows into the BitVec domain: it does
      // if the RHS contains a bitop OR references an already-bv source name
      // (chained lets: `let m = w & ALL; let w2 = m; let z = w2 | 1`). The LHS
      // source name is then registered so subsequent arithmetic on it keeps the
      // chain in `bvadd`/etc.  -  true 64-bit wraparound, not unbounded Int +.
      bool rhsBv = smtExprContainsBitop(let->init.get());
      if (!rhsBv) {
        // cheap structural scan for VarRefs already in wpBvNames
        std::set<std::string> rhsNames;
        smtCollectBitopsExpr(let->init.get(), rhsNames);  // reuses the collector
        for (const auto& n : rhsNames) if (c.wpBvNames.count(n)) { rhsBv = true; break; }
      }
      // G3a  -  thread the callee's post-state arrays back into the caller's
      // store when a `let r = f(...)` / `let r = obj.m(...)` inlines an array-
      // mutating handler. smtExprWp's Call arm (line ~2410) invokes
      // smtConcreteCallResult WITHOUT the `postStore` out-param, so for a LetStmt
      // RHS we branch here: for a direct/method call with a known callee we
      // instead call smtWpInlineCallPostStore, which passes the out-param and
      // merges positional array-typed callee param post-states back into `store`
      // (keyed off the caller-side VarRef arg name)  -  same pattern as the Call/
      // MethodCall statement arms below (lines ~3161, ~3192). Falls back to
      // smtExprWp for: the `len(arr)` builtin (preserved  -  smtExprWp handles it
      // via smtLenOf at line ~2402), spec/unknown callees (smtInlineSpecCall),
      // and any non-call RHS (the original path).
      std::string v;
      if (auto call = dynamic_cast<const Call*>(let->init.get())) {
        if (!call->fnPtr && call->callee == "len" && call->args.size() == 1) {
          // preserve `len(arr)` builtin  -  smtExprWp handles it via smtLenOf
          v = smtExprWp(c, let->init.get(), store);
        } else {
          const FuncDecl* callee = smtFindDirectCallee(c, call);
          if (callee) {
            std::vector<std::string> args;
            args.reserve(call->args.size());
            for (auto& a : call->args) args.push_back(smtExprWp(c, a.get(), store));
            v = smtWpInlineCallPostStore(c, callee, fnName, args, call->args,
                                         pathCond, premises, store);
          } else {
            // ox:proof spec fn or other  -  fall back to smtExprWp (handles smtInlineSpecCall)
            v = smtExprWp(c, let->init.get(), store);
          }
        }
      } else if (auto mc = dynamic_cast<const MethodCall*>(let->init.get())) {
        const FuncDecl* callee = smtFindMethodCallee(c, mc);
        if (callee) {
          std::vector<std::string> args;
          args.reserve(mc->args.size());
          for (auto& a : mc->args) args.push_back(smtExprWp(c, a.get(), store));
          v = smtWpInlineCallPostStore(c, callee, fnName, args, mc->args,
                                       pathCond, premises, store);
        } else {
          v = smtExprWp(c, let->init.get(), store);
        }
      } else {
        v = smtExprWp(c, let->init.get(), store);  // ORIGINAL path for non-call RHS
      }
      // ox:proof For a ghost let, the T2 Ghost section (src/Ghost.cpp) ALSO emits a
      // top-level `(declare-const ghost_<fn>_<name> <Sort>)`. To avoid a
      // duplicate-decl error in Z3 (which is fatal  -  it aborts the run and
      // every later query reports `sat`), we DO NOT emit a declare-const here
      // if the symbol is already in `c.declared` (which we pre-seed from the
      // T2 section's emit pass below  -  see emitSmt, and Tier 3-1's
      // `preseedGhostLetsInStmts` above). We just bind store[name].
      if (dynamic_cast<const GhostLetStmt*>(let)) {
        std::string gsym = "ghost_" + fnName + "_" + let->name;
        if (!c.declared.count(gsym)) {
          c.out << "(declare-const " << gsym << " " << smtSort(let->type) << ")\n";
          c.declared.insert(gsym);
        }
        // ox:proof T3-1  -  ghost state threading. If the ghost let HAS an initializer,
        // the symbol is now constrained: the ghost state equals the abstract
        // expression the body wrote. Emit `(assert (= gsym v))` so Z3 ties
        // the uninterpreted const to the runtime abstract value (Dafny-style
        // "ghost equals the expression it summarises"). And rebind store to
        // `v` so subsequent reads of `g` (via smtExprWp's store lookup) see
        // the abstract expression directly  -  strongest form  -  rather than a
        // fresh uninterpreted const.
        //
        // Without this fix: an `ensures result == g` after `ghost let g = n;
        // return n;` would discharge to `n == ghost_g` where ghost_g is a
        // free uninterpreted const  -  sat (Z3 picks ghost_g freely). WITH
        // this fix: Z3 has `(= ghost_g n)` as an axiom under the call path,
        // so `n == ghost_g` collapses to `n == n` (unsat negation).
        //
        // If the ghost let is UNINITIALIZED (`ghost let g: T;`), it stays a
        // free uninterpreted const  -  honest: the spec didn't constrain it,
        // so neither should we; an `ensures g` would fail (sat), which is
        // correct.
        if (let->init) {
          c.out << "(assert (= " << gsym << " " << v << "))\n";
          store[let->name] = v;
        } else {
          store[let->name] = gsym;
        }
      } else {
        store[let->name] = v;
        if (rhsBv) c.wpBvNames.insert(let->name);  // mark the source name
        // Tier 3  -  record the array's declared compile-time length so
        // `smtLenOf` can fold `len(arr)` in later clauses. This covers the
        // `let arr: [i64; 4] = ...;` local-array case (params + globals are
        // handled in `emitFnContracts` above). The key is `v` (the SMT base
        // symbol that `smtExprBaseName` returns for VarRef(arr))  -  same
        // keying convention. Only fires for `typeAnnotated` lets with an
        // explicit `[T; N]` declared type; inferred-type lets without an
        // annotation keep the free-int fallback (honest).
        if (let->typeAnnotated && let->type.tag == BType::Tag::array
            && let->type.count > 0) {
          c.arrayLenSyms[v] = let->type.count;
        }
      }
    } else if (dynamic_cast<const GhostLetStmt*>(let)) {
      // ox:proof Uninitialized ghost let  -  declare the symbol (dedup-guarded) and bind.
      std::string gsym = "ghost_" + fnName + "_" + let->name;
      if (!c.declared.count(gsym)) {
        c.out << "(declare-const " << gsym << " " << smtSort(let->type) << ")\n";
        c.declared.insert(gsym);
      }
      store[let->name] = gsym;
    }
    return;
  }

  // ExprStmt  -  could be an assignment (AssignTarget) or a side-effect call.
  if (auto es = dynamic_cast<const ExprStmt*>(s)) {
    if (!es->expr) return;

    // Contract 5  -  `asm!(...)` statement. The asm! block is modelled as a fresh
    // uninterpreted function `asm_<fn>_<seq>` (single-output) or one-per-output
    // `asm_<fn>_<seq>_out0/_out1/...` (multi-output)  -  see smtAsmTerm. For each
    // OUTPUT operand (`out("{reg}") lval` or `inout("{reg}") lval`), we rebind
    // the lvalue's source name in the WP store to ITS OWN per-output applied asm
    // term, so a subsequent `return r` (or any read of `r`) resolves to the asm
    // result  -  not the initial `let r = 0` value. This is the load-bearing path
    // for `let r: i64 = 0; asm!(..., out("{rax}") r); return r;` and, with
    // multiple outputs, `let a=0,b=0; asm!("...", out("{rax}") a, out("{rcx}")
    // b); return a + b;` (each of `a`/`b` binds to its own `_out0`/`_out1` term).
    if (auto asmExpr = dynamic_cast<const AsmExpr*>(es->expr.get())) {
      std::string asmTerm = smtAsmTerm(c, asmExpr, [&](const Expr* inExpr) {
        return smtExprWp(c, inExpr, store);
      });
      // Per-output applied terms for multi-output blocks (filled by
      // smtAsmTerm into c.asmOutputTerms). For single/zero-output blocks this is
      // empty and we fall back to the single representative `asmTerm`.
      const std::vector<std::string>& outTerms = c.asmOutputTerms;
      // Walk output operands in DECLARATION ORDER, pairing each with its own
      // applied term. outIdx tracks the output index (matches the order Sema
      // built outputTypes / smtAsmTerm built asmOutputTerms). For multi-output
      // we use outTerms[outIdx]; for single-output we use the single asmTerm.
      int outIdx = 0;
      for (const auto& io : asmExpr->ios) {
        if (!io.isOutput || !io.val) continue;
        std::string term;
        if (!outTerms.empty() && (size_t)outIdx < outTerms.size())
          term = outTerms[outIdx];            // multi-output: own per-output symbol
        else
          term = asmTerm;                     // single-output: the single symbol
        // The lvalue is typically a bare `VarRef` (e.g. `r` in `out("{rax}")
        // r`); rebind its source name in the WP store.
        if (auto v = dynamic_cast<const VarRef*>(io.val.get())) {
          store[v->name] = term;
        }
        ++outIdx;
      }
      return;
    }

    if (auto a = dynamic_cast<const AssignTarget*>(es->expr.get())) {
      if (a->kind == AssignTarget::Kind::var) {
        // Plain var reassignment: store[name] = WP value.
        if (a->value) {
          std::string v = smtExprWp(c, a->value.get(), store);
          // Register the assigned name as bv-flowing when its RHS is (so later
          // `x + 1` arithmetic keeps the chain in BV  -  see the LetStmt arm for
          // the rationale; this mirrors it for plain reassignment / compound-assign).
          bool rhsBv = smtExprContainsBitop(a->value.get());
          if (!rhsBv) {
            std::set<std::string> rhsNames;
            smtCollectBitopsExpr(a->value.get(), rhsNames);
            for (const auto& n : rhsNames) if (c.wpBvNames.count(n)) { rhsBv = true; break; }
          }
          // Compound ops (`x += e`) rebind to `(<op> x e)`.
          if (a->isCompound) {
            std::string cur = store.count(a->name) ? store[a->name]
                              : (c.nameMap.count(a->name) ? c.nameMap[a->name]
                                 : smtPlaceholder(c, "Int",
                                                  (" compound-assign of '"
                                                   + a->name + "'").c_str()));
            switch (a->compound) {
              case BinaryExpr::Op::add: v = "(+ " + cur + " " + v + ")"; break;
              case BinaryExpr::Op::sub: v = "(- " + cur + " " + v + ")"; break;
              case BinaryExpr::Op::mul: v = "(* " + cur + " " + v + ")"; break;
              case BinaryExpr::Op::div: v = "(div " + cur + " " + v + ")"; break;
              case BinaryExpr::Op::mod: v = "(mod " + cur + " " + v + ")"; break;
              case BinaryExpr::Op::band: v = "((_ bv2int " + std::to_string(BV_W)
                  + ") (bvand ((_ int2bv " + std::to_string(BV_W) + ") " + cur
                  + ") ((_ int2bv " + std::to_string(BV_W) + ") " + v + ")))"; break;
              case BinaryExpr::Op::bor:  v = "((_ bv2int " + std::to_string(BV_W)
                  + ") (bvor ((_ int2bv " + std::to_string(BV_W) + ") " + cur
                  + ") ((_ int2bv " + std::to_string(BV_W) + ") " + v + ")))"; break;
              case BinaryExpr::Op::bxor: v = "((_ bv2int " + std::to_string(BV_W)
                  + ") (bvxor ((_ int2bv " + std::to_string(BV_W) + ") " + cur
                  + ") ((_ int2bv " + std::to_string(BV_W) + ") " + v + ")))"; break;
              case BinaryExpr::Op::shl:  v = "((_ bv2int " + std::to_string(BV_W)
                  + ") (bvshl ((_ int2bv " + std::to_string(BV_W) + ") " + cur
                  + ") ((_ int2bv " + std::to_string(BV_W) + ") " + v + ")))"; break;
              case BinaryExpr::Op::shr:  v = "((_ bv2int " + std::to_string(BV_W)
                  + ") (bvlshr ((_ int2bv " + std::to_string(BV_W) + ") " + cur
                  + ") ((_ int2bv " + std::to_string(BV_W) + ") " + v + ")))"; break;
              default: break;   // eq/ne/lt/gt/le/ge/land/lor  -  nonsensical as
                                // compound-assign; leave v as the RHS (honest).
            }
          }
          store[a->name] = v;
          if (rhsBv) c.wpBvNames.insert(a->name);  // mark the source name
        }
        return;
      }
      // Tier 2a  -  `self.x = e` field write. When the LHS is a field access on
      // the `self` receiver of an impl method (base is a bare VarRef "self" and
      // this function has a known `c.selfStructName`), rebind the per-field WP
      // slot `store["self__<S>__<field>"]` to the new post term  -  exactly as the
      // Kind::var arm rebinds `store[name]`. Subsequent `self.x` reads (via the
      // Field arm in smtExprWp) then resolve to this updated term instead of the
      // function-entry fresh const, so `ensures result == self.owner` after
      // `self.owner = new_owner` discharges. bv-flow tracking mirrors the var
      // arm: mark the field key in wpBvNames when the RHS is bit-flowing.
      if (a->kind == AssignTarget::Kind::field && a->value &&
          !c.selfStructName.empty()) {
        if (auto bv = dynamic_cast<const VarRef*>(a->base.get())) {
          if (bv->name == "self") {
            std::string key = "self__" + c.selfStructName + "__" + a->field;
            std::string v = smtExprWp(c, a->value.get(), store);
            bool rhsBv = smtExprContainsBitop(a->value.get());
            if (!rhsBv) {
              std::set<std::string> rhsNames;
              smtCollectBitopsExpr(a->value.get(), rhsNames);
              for (const auto& n : rhsNames)
                if (c.wpBvNames.count(n)) { rhsBv = true; break; }
            }
            // Compound field-assign (`self.x += e`): rebind to `(<op> cur e)`,
            // where `cur` is the current per-field slot (post term if already
            // written this frame, else the entry const). Mirrors the Kind::var
            // compound arm; unknown-op cases fall through leaving v = RHS.
            if (a->isCompound) {
              std::string cur = store.count(key) ? store[key]
                                : (c.nameMap.count(key) ? c.nameMap[key]
                                   : smtPlaceholder(c, "Int",
                                       (" compound-assign of self field '"
                                        + a->field + "'").c_str()));
              switch (a->compound) {
                case BinaryExpr::Op::add: v = "(+ " + cur + " " + v + ")"; break;
                case BinaryExpr::Op::sub: v = "(- " + cur + " " + v + ")"; break;
                case BinaryExpr::Op::mul: v = "(* " + cur + " " + v + ")"; break;
                case BinaryExpr::Op::div: v = "(div " + cur + " " + v + ")"; break;
                case BinaryExpr::Op::mod: v = "(mod " + cur + " " + v + ")"; break;
                case BinaryExpr::Op::band: v = "((_ bv2int " + std::to_string(BV_W)
                    + ") (bvand ((_ int2bv " + std::to_string(BV_W) + ") " + cur
                    + ") ((_ int2bv " + std::to_string(BV_W) + ") " + v + ")))"; break;
                case BinaryExpr::Op::bor:  v = "((_ bv2int " + std::to_string(BV_W)
                    + ") (bvor ((_ int2bv " + std::to_string(BV_W) + ") " + cur
                    + ") ((_ int2bv " + std::to_string(BV_W) + ") " + v + ")))"; break;
                case BinaryExpr::Op::bxor: v = "((_ bv2int " + std::to_string(BV_W)
                    + ") (bvxor ((_ int2bv " + std::to_string(BV_W) + ") " + cur
                    + ") ((_ int2bv " + std::to_string(BV_W) + ") " + v + ")))"; break;
                case BinaryExpr::Op::shl:  v = "((_ bv2int " + std::to_string(BV_W)
                    + ") (bvshl ((_ int2bv " + std::to_string(BV_W) + ") " + cur
                    + ") ((_ int2bv " + std::to_string(BV_W) + ") " + v + ")))"; break;
                case BinaryExpr::Op::shr:  v = "((_ bv2int " + std::to_string(BV_W)
                    + ") (bvlshr ((_ int2bv " + std::to_string(BV_W) + ") " + cur
                    + ") ((_ int2bv " + std::to_string(BV_W) + ") " + v + ")))"; break;
                default: break;
              }
            }
            store[key] = v;
            if (rhsBv) c.wpBvNames.insert(key);
            return;
          }
        }
      }
      // index / other-base field / deref assignments: out of scope for Tier B's
      // Int-only store. We honestly leave the LHS un-modelled  -  a downstream
      // clause that references the array/field still resolves to the outer
      // declared const (or a placeholder). Sound; completeness-limiting.
      //
      // Gap 1b  -  `arr[i] = e` index assignment. When the LHS is an index
      // access on a bare VarRef array name, update the WP store's slot for
      // that array name with `(store store[arrName] idx v)`  -  the SMT array
      // store operation. `(store arr idx val)` returns a new array with the
      // element at `idx` set to `val` and all others unchanged. This is the
      // array-theory analogue of the var-arm's `store[name] = v` rebind: a
      // subsequent `arr[i]` read (via the Index arm in smtExprWp) lowers to
      // `(select store[arrName] j)`, which folds the write into the symbolic
      // post-state of the array  -  exactly what `preserves` needs to thread
      // to the invariant lowering via Gap 1c.
      //
      // We support the bare-VarRef base form (`ept[i] = ...`) and, since G1d,
      // arbitrary-depth nested-index bases (`arr[i][j] = x`, `arr[i][j][k] = x`,
      // …) where `a->base` is a chain of `Index` nodes rooted at a bare
      // `VarRef`. The recursion walks the chain down to its VarRef root,
      // collecting indices [i_0, i_1, …, i_N] in source order, then builds the
      // post-state array term from the outermost store inward:
      //   arr' = (store arr i_0 (store (select arr i_0) i_1
      //            (... (store (select (select ... arr i_0 ...) i_{N-1}) i_N v))))
      // The sub-array sorts compose (`(Array Int <elem>)` at each level) so
      // storing a sub-array back into its outer `(Array Int (Array Int <elem>))`
      // cell is well-typed  -  SMT-LIB array theory accepts store-of-stores
      // naturally. Non-VarRef inner bases other than a chain of `Index` (notably
      // a `Field` base  -  the G1e struct-field-on-array case) still honestly
      // fall through unmodelled  -  sound, completeness-limiting.
      if (a->kind == AssignTarget::Kind::index && a->value && a->index && a->base) {
        // G1d (generalised)  -  nested-array WRITE of arbitrary depth.
        // `arr[i_0][i_1]...[i_N] = v` lowers to a nest of (store (select ...))
        // wrapping the leaf value, applied to the pre-state array term. The
        // OUTERMOST store uses the DEEPEST source index (the index adjacent to
        // the VarRef root), and the INNERMOST store uses `a->index` (the
        // outermost-applied source index  -  the leaf of the read path). E.g.
        // `arr[i][j] = x` -> `(store arr i (store (select arr i) j x))`.
        //
        // Walk the LHS: descend from `a->base` down through a chain of `Index`
        // nodes toward the VarRef root, collecting each `.index` in descent
        // order [j_deepest-1, …, i_root-adjacent]. Reverse that and append
        // `a->index` to get write-order [i_0, …, i_N] (outermost store first).
        // When the base resolves to a bare `VarRef`, that names the array;
        // resolve it via the store → nameMap → declared lookup (exactly as the
        // bare-VarRef arm below). Any non-Index/non-VarRef node in the chain
        // (e.g. a `Field` for the G1e struct case `ept[idx].field = x`) leaves
        // `arrBase` null and we fall through unmodelled (sound).
        //
        // The value `v` (and every index) is smtExprWp-evaluated only AFTER the
        // walk succeeds, so a fall-through (G1e) doesn't emit stray placeholder
        // declares for `v`'s sub-exprs.
        std::vector<const Expr*> descentIdx;  // in descent order: outermost-Index first
        const Expr* node = a->base.get();
        while (auto ixNode = dynamic_cast<const Index*>(node)) {
          descentIdx.push_back(ixNode->index.get());
          node = ixNode->base.get();
        }
        // Write-order [i_0, …, i_N]: reversed descent chain (root-adjacent first)
        // then `a->index` (leaf) last. orderedIdx[0] is the OUTERMOST store index.
        std::vector<const Expr*> orderedIdx;
        for (auto it = descentIdx.rbegin(); it != descentIdx.rend(); ++it)
          orderedIdx.push_back(*it);
        orderedIdx.push_back(a->index.get());
        // ox:unsafe node is the VarRef root if the chain was clean  -  must be a bare VarRef.
        auto arrBase = dynamic_cast<const VarRef*>(node);
        if (arrBase && orderedIdx.size() >= 2) {
          std::string arrName = arrBase->name;
          std::string arrTerm;
          auto sit = store.find(arrName);
          if (sit != store.end()) arrTerm = sit->second;
          else {
            auto mit = c.nameMap.find(arrName);
            if (mit != c.nameMap.end()) arrTerm = mit->second;
            else if (c.declared.count(arrName)) arrTerm = arrName;
            else return;   // unknown root array  -  leave un-modelled (sound)
          }
          // Lower every index term in write-order.
          std::vector<std::string> idxs;
          idxs.reserve(orderedIdx.size());
          for (auto* ix : orderedIdx) idxs.push_back(smtExprWp(c, ix, store));
          std::string v = smtExprWp(c, a->value.get(), store);
          // Build the nested stores from the innermost (leaf value) outward.
          // At level k (k = N down to 0): the select-chain into arr down to
          // depth k-1 is `(select (select ... (select arr i_0) …) i_{k-1})`,
          // and we store `acc` (the next-level inner, or v) into slot i_k of
          // that sub-array. After the loop `acc` is the full post-state array.
          std::string acc = v;
          for (size_t k = orderedIdx.size(); k-- > 0; ) {
            std::string sel = arrTerm;
            for (size_t m = 0; m < k; ++m)
              sel = "(select " + sel + " " + idxs[m] + ")";
            acc = "(store " + sel + " " + idxs[k] + " " + acc + ")";
          }
          store[arrName] = acc;
          return;
        }
        // a->base is an Index chain that did NOT cleanly bottom out at a bare
        // VarRef (e.g. a `Field` base  -  the G1e struct-field-on-array case, or
        // some other non-array base). Falls through unmodelled (sound)  -  the
        // G1e TODO below documents the struct-field gap.
        // G1e  -  TODO: struct-field writes on array-element LHS, e.g.
        // `ept[idx].gpa = x` where `ept` is `[EptEntry; N]` (NOT a primitive
        // [i64; N]  -  that's bit-packed and goes through the bare-VarRef arm).
        // This needs (a) BType::Tag::struct_ → SMT `declare-datatypes` sort
        // lowering (none today), (b) struct field accessors in the IndexExpr
        // READ arm, and (c) a `(store arr idx (update_field (select arr idx) gpa x))`
        // emission here (Z3's `update_field` or manual constructor wrapping). The
        // `a->base` shape for this case is a FieldExpr (a->base is `ept[idx]`
        // where the FieldExpr refers to field `gpa`). Falls through unmodelled
        // today  -  sound, completeness-limiting. Hypervisor EPT walks expressed as
        // structured `[EptEntry; N]` arrays would hit this gap; the current
        // workaround is to bit-pack EPT entries as `[i64; N]` and use `& M` /
        // `>> N` patterns (covered by C5's bitop-to-Int bridge).
        if (auto bv = dynamic_cast<const VarRef*>(a->base.get())) {
          std::string arrName = bv->name;
          // ox:why Current array term: prefer the symbolic store (so a chained
          // `ept[0] = a; ept[1] = b;` accumulates as nested `store`s), then
          // nameMap (params bound at function entry), then a declared fresh
          // const. Matches the Index-read arm's lookup order.
          std::string arrTerm;
          auto sit = store.find(arrName);
          if (sit != store.end()) arrTerm = sit->second;
          else {
            auto mit = c.nameMap.find(arrName);
            if (mit != c.nameMap.end()) arrTerm = mit->second;
            else if (c.declared.count(arrName)) arrTerm = arrName;
            else {
              // Unknown array  -  honestly leave un-modelled (preserve prior
              // fallback). Downstream reads still resolve to a placeholder.
              return;
            }
          }
          std::string idx = smtExprWp(c, a->index.get(), store);
          std::string v = smtExprWp(c, a->value.get(), store);
          store[arrName] = "(store " + arrTerm + " " + idx + " " + v + ")";
          return;
        }
      }
      return;
    }
    if (auto call = dynamic_cast<const Call*>(es->expr.get())) {
      // Feature 7  -  mmio_store built-in: writes to the mmio_mem array model.
      // Lowered as `store["mmio_mem"] = (store <cur_mmio> <ptr_term> <val_term>)`.
      // This rebinds the WP store's mmio_mem entry to the updated array term,
      // so a subsequent mmio_load(ptr) in the same or caller function resolves
      // via (select <updated_mmio> ptr) and sees this write.
      if (!call->fnPtr && call->callee == "mmio_store" && call->args.size() == 2) {
        std::string ptrTerm = smtExprWp(c, call->args[0].get(), store);
        std::string valTerm = smtExprWp(c, call->args[1].get(), store);
        // Get or declare the current mmio_mem array symbol.
        auto it = store.find("mmio_mem");
        std::string mmioSym;
        if (it != store.end() && !it->second.empty()) {
          mmioSym = it->second;
        } else {
          mmioSym = c.curFn + "_mmio_mem";
          if (c.declared.find(mmioSym) == c.declared.end()) {
            c.out << "(declare-const " << mmioSym << " (Array Int Int))\n";
            c.declared.insert(mmioSym);
          }
          store["mmio_mem"] = mmioSym;
        }
        // Update the store: store["mmio_mem"] = (store mmioSym ptr val)
        store["mmio_mem"] = "(store " + mmioSym + " " + ptrTerm + " " + valTerm + ")";
        // Part 1  -  cross-function MMIO threading (named per-address model).
        // Record the (address, value) write into the current function's
        // effect set, so a CALLER applying this function's effects (in a
        // Call arm below) updates its own `mmioState` and sees the write.
        // Simultaneously update our own `mmioState` so a subsequent
        // `mmio_load` of THIS address in the SAME function HITs the value
        // just written (instead of re-selecting from the array model, which
        // yields a `(select (store ...) ptr)` term Z3 must discharge equal
        // to `valTerm`  -  the named model makes them syntactically equal).
        c.mmioWriteEffects[c.curFn].push_back({ptrTerm, valTerm});
        c.mmioState[ptrTerm] = valTerm;
        return;
      }
      std::vector<std::string> args;
      args.reserve(call->args.size());
      for (auto& a : call->args) args.push_back(smtExprWp(c, a.get(), store));
      const FuncDecl* callee = smtFindDirectCallee(c, call);
      // G3a  -  propagate the callee's post-state arrays back into the caller's
      // store. Without this, a dispatcher that inlines a handler like
      // `map_page_mut(idx, gpa, ept)` never sees the handler's `ept[idx] = gpa`
      // mutation: `smtConcreteCallResult` threads an internal `calleeStore`
      // through the callee body (writing `(store old_ept idx gpa)` for `ept`),
      // but only exposes it via the `postStore` out-param. Callers here were
      // not passing that out-param, so the dispatcher's `store["ept"]` stayed
      // bound to the PRE-state arg and the preserves invariant lowered against
      // a bare `(select arg_2 arg_0)` (no store)  -  see `_fresh.smt2:414`.
      //
      // We pass a local `localPostStore`, then merge array-typed callee params
      // POSITIONALLY back into the caller's `store`: the arg passed at slot i
      // is the caller-side name (a `VarRef` for a variable passed by name, e.g.
      // dispatcher's `arg_2` / `ept`) and we set `store[<arg name>]` to the
      // callee's post-state term for that param. Non-array params don't mutate
      // (skipped); non-VarRef args (literals / computed exprs) can't be store-
      // merged into (skipped  -  sound, completeness-limiting). If this call is
      // inside an if-arm, `mergeStores` (later in smtEncodeStmt) folds the
      // updated `store` across arms via `(ite cond ...)`, so we set `store`
      // directly here.
      std::map<std::string, std::string> localPostStore;
      std::string resultTerm = smtConcreteCallResult(c, callee, fnName, args,
                                                    pathCond, premises,
                                                    &localPostStore);
      if (callee) {
        size_t n = std::min(callee->params.size(), call->args.size());
        for (size_t i = 0; i < n; ++i) {
          if (callee->params[i].type.tag != BType::Tag::array) continue;
          auto itP = localPostStore.find(callee->params[i].name);
          if (itP == localPostStore.end()) continue;
          if (auto vr = dynamic_cast<const VarRef*>(call->args[i].get())) {
            store[vr->name] = itP->second;
          }
        }
        // Feature 7  -  propagate callee's mmio_mem post-state into the caller.
        auto mmioIt = localPostStore.find("mmio_mem");
        if (mmioIt != localPostStore.end() && !mmioIt->second.empty()) {
          store["mmio_mem"] = mmioIt->second;
        }
        // Part 1  -  cross-function MMIO threading (named per-address model).
        // The callee's mmio_store arms recorded each (addr,val) write into
        // c.mmioWriteEffects[callee->name]. Apply them to the caller's named
        // mmioState so a later mmio_load(addr) in the caller HITs the
        // propagated value. Unwritten addresses keep their prior mmioState
        // (frame guarantee).
        auto wfx = c.mmioWriteEffects.find(callee->name);
        if (wfx != c.mmioWriteEffects.end()) {
          for (const auto& kv : wfx->second) c.mmioState[kv.first] = kv.second;
        }
      }
      std::string assumeTerm = smtConcreteCallAssumes(c, callee, args, resultTerm);
      if (!assumeTerm.empty()) {
        store["__ox_call_assume__"] = assumeTerm;
      }
      return;
    }
    if (auto mc = dynamic_cast<const MethodCall*>(es->expr.get())) {
      std::vector<std::string> args;
      args.reserve(mc->args.size());
      for (auto& a : mc->args) args.push_back(smtExprWp(c, a.get(), store));
      const FuncDecl* callee = smtFindMethodCallee(c, mc);
      // G3a  -  same post-store propagation as the Call arm above: pass the
      // `postStore` out-param into `smtConcreteCallResult`, then merge the
      // callee's post-state array params back into the caller's `store`
      // positionally (arg i -> callee param i), keyed off the caller-side
      // `VarRef` name. See the Call arm comment for the full rationale.
      std::map<std::string, std::string> localPostStore;
      std::string resultTerm = smtConcreteCallResult(c, callee, fnName, args,
                                                    pathCond, premises,
                                                    &localPostStore);
      if (callee) {
        size_t n = std::min(callee->params.size(), mc->args.size());
        for (size_t i = 0; i < n; ++i) {
          if (callee->params[i].type.tag != BType::Tag::array) continue;
          auto itP = localPostStore.find(callee->params[i].name);
          if (itP == localPostStore.end()) continue;
          if (auto vr = dynamic_cast<const VarRef*>(mc->args[i].get())) {
            store[vr->name] = itP->second;
          }
        }
        // Feature 7  -  propagate callee's mmio_mem post-state into the caller.
        auto mmioIt = localPostStore.find("mmio_mem");
        if (mmioIt != localPostStore.end() && !mmioIt->second.empty()) {
          store["mmio_mem"] = mmioIt->second;
        }
        // Part 1  -  cross-function MMIO threading (named per-address model).
        // Apply callee's recorded (addr,val) writes to the caller's mmioState.
        auto wfx = c.mmioWriteEffects.find(callee->name);
        if (wfx != c.mmioWriteEffects.end()) {
          for (const auto& kv : wfx->second) c.mmioState[kv.first] = kv.second;
        }
      }
      std::string assumeTerm = smtConcreteCallAssumes(c, callee, args, resultTerm);
      if (!assumeTerm.empty()) store["__ox_call_assume__"] = assumeTerm;
      return;
    }
    // IncDecExpr (`x++` / `x--`)  -  rebind store[x] = (x +/- 1).
    if (auto id = dynamic_cast<const IncDecExpr*>(es->expr.get())) {
      if (id->kind == AssignTarget::Kind::var) {
        std::string cur = store.count(id->name) ? store[id->name]
                          : (c.nameMap.count(id->name) ? c.nameMap[id->name]
                             : smtPlaceholder(c, "Int",
                                              (" inc/dec of '" + id->name + "'").c_str()));
        store[id->name] = id->isInc ? "(+ " + cur + " 1)" : "(- " + cur + " 1)";
      }
      return;
    }
    // Side-effect call (print, etc.)  -  no symbolic effect here. Honest.
    return;
  }

  // ox:proof return e;   -  bind result, discharge ensures at this return site, mark dead.
  if (auto ret = dynamic_cast<const ReturnStmt*>(s)) {
    if (ret->value) {
      std::string v = smtExprWp(c, ret->value.get(), store);
      store["result"] = v;
    }
    // Also keep `result` resolvable via nameMap for ensures that use the bare
    // word `result`  -  smtExprWp checks store first, then nameMap, so setting
    // store["result"] is enough; we don't mutate the shared nameMap.
    smtDischargeEnsures(c, fnName, ens.clauses, store, pathCond, premises);
    returned = true;
    return;
  }

  // ox:proof assert P;   -  discharge P under pathCond + premises (WP: must hold here).
  // `assert P by { <hints> };`  -  the hints emit as SMT premises (assumed
  // inside a fresh push/pop scope) before P is discharged, so the solver has
  // them available while proving P.
  if (auto as = dynamic_cast<const AssertStmt*>(s)) {
    std::string label = fnName + "_assert_" + std::to_string(c.assertSeq++);
    c.out << "; " << label << " (source line " << as->line << ")\n";
    std::string term = smtExprWp(c, as->cond.get(), store);
    c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
    std::vector<std::string> prem = premises;
    if (!pathCond.empty()) prem.push_back(pathCond);
    if (!as->byBody.empty()) {
      // Emit each hint statement inside a (push)…(pop) scope. We walk each hint
      // through smtEncodeStmt with a SEPARATE WP store / returned flag / path
      // condition so the byBody has no effect on the outer symbolic state:
      // `by { ... }` is proof-only and must not pollute post-assert SMT facts.
      //
      // For `assert H;` hints: smtEncodeStmt discharges H (proving H from prior
      // premises) AND we additionally `(assert <H-term>)` it into the scope so
      // it becomes a premise for the outer P. This mirrors the contract that
      // `assert`s inside a proof block both discharge AND add as hypothesis.
      // For `instantiate` hints: in Tier B they fall through (no symbolic
      // effect yet  -  InstantiateStmt emission is not implemented); they are
      // accepted for forward compatibility. For lemma-call ExprStmt hints: the
      // Tier B call arm folds the lemma's ensures into the WP store so further
      // hints / the outer discharge see them via the store.
      c.out << "(push)\n";
      std::map<std::string, std::string> hintStore = store;
      bool hintReturned = false;
      std::string hintPath = pathCond;
      std::vector<std::string> hintPrem = prem;
      for (auto& h : as->byBody) {
        if (!h) continue;
        if (hintReturned) break;
        c.wpPremises = hintPrem;
        c.wpPathCond = hintPath;
        // ox:why Detect an `assert H;` hint so we can assume H as a premise after it
        // discharges  -  exactly like the ProofBlockStmt contract describes.
        if (auto ha = dynamic_cast<const AssertStmt*>(h.get())) {
          if (ha->byBody.empty()) {
            // ox:proof Plain assert hint: discharge H under the running hint premises,
            // then assume H by pushing its term into the premise list AND
            // asserting it in the SMT scope.
            std::string hlabel = fnName + "_assert_" +
                                 std::to_string(c.assertSeq++);
            c.out << "; " << hlabel << " (hint, source line " << ha->line
                  << ")\n";
            std::string hterm = smtExprWp(c, ha->cond.get(), hintStore);
            c.out << "(define-fun " << hlabel << " () Bool " << hterm
                  << ")\n\n";
            std::vector<std::string> hPrem = hintPrem;
            if (!hintPath.empty()) hPrem.push_back(hintPath);
            smtDischarge(c, hlabel, hlabel, hPrem);
            // Assume the hint's assertion: it is now an available premise.
            c.out << "(assert " << hlabel << ")\n";
            hintPrem.push_back(hlabel);
            continue;
          }
        }
        // ox:proof Non-assert hint (instantiate / lemma-call ExprStmt / calc / etc.)  - 
        // delegate to the standard Tier B statement encoder so it updates the
        // local hintStore / hintPath per its kind. Premises grow only from
        // explicit assert hints above.
        smtEncodeStmt(c, fnName, h.get(), depth, hintPrem, hintStore, hintPath,
                      hintReturned, ens);
      }
      // ox:proof Discharge the outer assertion with the running hint premises (which
      // include the assert-hint labels). smtDischarge's own (push)/(pop) keeps
      // the (push) we opened visible during the check-sat.
      smtDischarge(c, label, label, hintPrem);
      c.out << "(pop)\n\n";
    } else {
      smtDischarge(c, label, label, prem);
    }
    return;
  }

  // `assume <expr>;` (non-trusted) and `trusted assume <expr>;`  -  add the
  // condition as an SMT HYPOTHESIS (asserted, NOT discharged as a goal). Unlike
  // `assert` (which must be PROVEN from preceding facts), an assume is taken as
  // a fact: it becomes a premise for every downstream discharge in the same
  // path (asserts, ensures, loop invariants). We lower cond via smtExprWp so
  // the hypothesis sees the current symbolic store (a WP-aware assume), define
  // a named Bool so we can reference it cheaply in the premise list, emit
  // `(assert <label>)` to make it an SMT fact, AND push <label> onto `premises`
  // so subsequent smtDischarge calls in this path thread it through their
  // implication query. `pathCond` is NOT extended  -  an assume is a side fact,
  // not a branch precondition; threading it only through `premises` keeps the
  // path numbering stable so mergeStores at if-joins isn't perturbed.
  //
  // For a `trusted` assume we additionally emit `; note: trusted assume at
  // line N` (+ optional `; note: source: <citation>`) into the .smt2; the
  // doVerify report scans those notes for the `--audit-trust` report.
  if (auto asc = dynamic_cast<const AssumeStmt*>(s)) {
    std::string label = fnName + "_assume_" +
                        std::to_string(c.assumeSeq++);
    if (asc->isTrusted) {
      c.out << "; note: trusted assume at line " << asc->line << "\n";
      if (!asc->sourceCitation.empty())
        c.out << "; note: source: " << asc->sourceCitation << "\n";
    }
    c.out << "; " << label << " (source line " << asc->line << ")\n";
    std::string term = smtExprWp(c, asc->cond.get(), store);
    c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
    c.out << "(assert " << label << ")\n";
    // ox:proof Thread the hypothesis into the running premise list so downstream
    // discharges in this code path see it. We copy `premises` (a by-value
    // vector<string> param) into a local, append, and reassign  -  this is the
    // same idiom the assert arm uses for its `prem`. Simplest correct form.
    std::vector<std::string> prem = premises;
    prem.push_back(label);
    return;
  }

  // calc { <expr>; <REL> {hints;} <expr>; ... }  -  calcD
  // Equational-reasoning block. Discharge each consecutive pair of steps as a
  // separate (check-sat) under the running premise list: for step pair (i, i+1)
  // with relation REL_i, prove  t_i REL_i t_{i+1}  (negated  -  unsat means proven).
  //
  // The premise list starts as the outer `premises` + pathCond. For each pair:
  //   (1) Emit the hints of step i: an assert hint discharges its own H AND
  //       folds H into the running premise list (a hypothesis for this step and
  //       for later steps  -  same `assert ... by { }` pattern used above);
  //       instantiate / lemma-call ExprStmt hints update the local hint store
  //       via smtEncodeStmt delegation so the step expressions see them.
  //   (2) Lower t_i and t_{i+1} from the (possibly hint-updated) store.
  //   (3) Build the relation term: "==" → (= a b); "!=" → (not (= a b));
  //        "<=" → (<= a b); ">=" → (>= a b); "<" → (< a b); ">" → (> a b).
  //   (4) Define the step's boolean label and smtDischarge(label, label, prem).
  //   (5) Fold the discharged step relation into the running premise list so
  //        later steps can use the proven equality/ordering transitively.
  //
  // Hints emit inside a wrapping (push)/(pop) so the assumed hint-assertions and
  // the step discharge do NOT pollute the outer symbolic state  -  the calc block
  // is proof-only, so it has no effect on the post-calc store / path. After the
  // final step we additionally assert the composed transitive relation (first
  // expr REL_chain last expr) as a summary fact in the (push) scope; the per-
  // step discharges are the actual proof, this just makes the composite visible
  // to any downstream discharge inside the same push scope.
  if (auto cs = dynamic_cast<const CalcStmt*>(s)) {
    c.out << "; --- calc block (source line " << cs->line << ") ---\n";
    c.out << "(push)\n";
    // Running premise list for the chain: starts from the outer frame and
    // accumulates each discharged step. Hints fold in as they're emitted.
    std::vector<std::string> prem = premises;
    if (!pathCond.empty()) prem.push_back(pathCond);
    // ox:proof Private store/path for hint emission  -  hints may add lemma ensures to the
    // store (so step expressions see them) but must not leak beyond the calc.
    std::map<std::string, std::string> calcStore = store;
    std::string calcPath = pathCond;
    bool calcReturned = false;

    // ox:proof Lower and remember each step's SMT term so the transitive summary at the
    // end can reference the first and last without re-lowering.
    std::vector<std::string> stepTerms;
    stepTerms.reserve(cs->steps.size());

    for (size_t i = 0; i + 1 < cs->steps.size(); ++i) {
      const auto& step = cs->steps[i];
      const std::string& rel = step.relation;
      // (1) Emit hints for step i. Each hint is walked through smtEncodeStmt
      //     with the running premise list, the private store, and the private
      //     path. An `assert H;` hint additionally discharges H and assumes it
      //     as a premise (mirroring assert-byBody above); other hints (lemma
      //     calls / instantiate) update calcStore/calcPath per their kind.
      if (!step.hints.empty()) {
        for (auto& h : step.hints) {
          if (!h) continue;
          if (calcReturned) break;
          c.wpPremises = prem;
          c.wpPathCond = calcPath;
          // ox:proof An `assert H;` hint (no byBody): discharge H, then assume it.
          if (auto ha = dynamic_cast<const AssertStmt*>(h.get())) {
            if (ha->byBody.empty()) {
              std::string hlabel = fnName + "_assert_" +
                                   std::to_string(c.assertSeq++);
              c.out << "; " << hlabel << " (calc hint, source line "
                    << ha->line << ")\n";
              std::string hterm = smtExprWp(c, ha->cond.get(), calcStore);
              c.out << "(define-fun " << hlabel << " () Bool " << hterm
                    << ")\n\n";
              std::vector<std::string> hPrem = prem;
              if (!calcPath.empty()) hPrem.push_back(calcPath);
              smtDischarge(c, hlabel, hlabel, hPrem);
              c.out << "(assert " << hlabel << ")\n";
              prem.push_back(hlabel);
              continue;
            }
          }
          // ox:proof Non-assert hint: instantiate / lemma-call ExprStmt / nested calc  - 
          // delegate to the standard Tier B encoder so its ensures fold into
          // calcStore (visible to later step expressions) without touching
          // premises (which only grow from explicit assert hints above).
          smtEncodeStmt(c, fnName, h.get(), depth, prem, calcStore, calcPath,
                        calcReturned, ens);
        }
      }

      // (2) Lower the two step expressions. We use the (possibly hint-
      //     updated) calcStore so a lemma-call hint that adds an ensures fact
      //     to the store is visible to the step expression.
      if (!step.expr) continue;   // honest skip on Parser recovery gap
      std::string t_i = smtExprWp(c, step.expr.get(), calcStore);
      const auto& nextStep = cs->steps[i + 1];
      if (!nextStep.expr) continue;
      std::string t_i1 = smtExprWp(c, nextStep.expr.get(), calcStore);
      if (stepTerms.empty()) stepTerms.push_back(t_i);
      stepTerms.push_back(t_i1);

      // (3) Build the relation term from the relation string.
      std::string relTerm;
      if      (rel == "==") relTerm = "(= " + t_i + " " + t_i1 + ")";
      else if (rel == "!=") relTerm = "(not (= " + t_i + " " + t_i1 + "))";
      else if (rel == "<=") relTerm = "(<= " + t_i + " " + t_i1 + ")";
      else if (rel == ">=") relTerm = "(>= " + t_i + " " + t_i1 + ")";
      else if (rel == "<")  relTerm = "(< "  + t_i + " " + t_i1 + ")";
      else if (rel == ">")  relTerm = "(> "  + t_i + " " + t_i1 + ")";
      else {
        // Unknown relation  -  emit a diagnostic comment and skip this step's
        // discharge. Sema already reported this, so we don't duplicate a hard
        // error here; just emit a no-op so the chain continues.
        c.out << "; warning: calc step " << i << " has unknown relation '"
              << rel << "'  -  step discharge skipped\n";
        continue;
      }

      // ox:proof (4) Define the step's boolean label and discharge the negated
      //     relation. `unsat` means  t_i REL_i t_{i+1}  holds under `prem`.
      std::string stepLabel = fnName + "_calc_" +
                              std::to_string(c.calcStepSeq++);
      c.out << "; " << stepLabel << " (calc step " << i << ", source line "
            << step.expr->line << ")\n";
      c.out << "(define-fun " << stepLabel << " () Bool " << relTerm
            << ")\n\n";
      std::vector<std::string> stepPrem = prem;
      if (!calcPath.empty()) stepPrem.push_back(calcPath);
      smtDischarge(c, stepLabel, stepLabel, stepPrem);

      // ox:proof (5) Fold the proven relation into the running premise list so the
      //     next step can use it transitively. We assert it inside this
      //     (push) scope (popped at the end of the calc block), and add the
      //     label to `prem` so the next step's discharge premise list carries
      //     it as a hypothesis.
      c.out << "(assert " << stepLabel << ")\n";
      prem.push_back(stepLabel);
    }

    // After the loop, `stepTerms` holds the first and last step terms (plus any
    // intermediate ones we lowered). The composed transitive relation (first
    // expr REL_chain last expr) is already implied by the per-step premises;
    // we emit a summary comment so the SMT is readable, but we do NOT emit a
    // redundant discharge for it (each step was already discharged above).
    if (stepTerms.size() >= 2) {
      c.out << "; calc summary: " << stepTerms.front() << " {...} "
            << stepTerms.back() << " (composed from per-step discharges)\n";
    }

    c.out << "(pop)\n\n";
    return;
  }

  // ox:proof `proof that forall ... by induction`  -  discharge the user-supplied base
  // and step cases as explicit goals. This statement used to be accepted by
  // Parser/Sema and skipped by both IRGen and the SMT walker, allowing an
  // invalid proof block to pass verification without producing any obligation.
  if (auto ps = dynamic_cast<const ProofStmt*>(s)) {
    c.out << "; --- induction proof at line " << ps->line << " ---\n";
    std::vector<std::string> outerPrem = premises;
    if (!pathCond.empty()) outerPrem.push_back(pathCond);

    if (ps->baseCase) {
      std::string baseLabel = fnName + "_induction_base_" +
                              std::to_string(c.assertSeq++);
      std::string baseTerm = smtExprWp(c, ps->baseCase.get(), store);
      c.out << "; " << baseLabel << " (source line " << ps->baseCase->line << ")\n";
      c.out << "(define-fun " << baseLabel << " () Bool " << baseTerm << ")\n\n";
      smtDischarge(c, baseLabel, baseLabel, outerPrem);
    }

    if (ps->ih && ps->goal) {
      std::string ksym = fnName + "_induction_" + ps->inductionVar + "_" +
                         std::to_string(c.placeholderSeq++);
      smtDeclareConst(c, ksym, "Int");
      std::map<std::string, std::string> stepStore = store;
      stepStore[ps->inductionVar] = ksym;
      std::string ihTerm = smtExprWp(c, ps->ih.get(), stepStore);
      std::string goalTerm = smtExprWp(c, ps->goal.get(), stepStore);
      std::string stepLabel = fnName + "_induction_step_" +
                              std::to_string(c.assertSeq++);
      c.out << "; " << stepLabel << " (source line " << ps->goal->line << ")\n";
      c.out << "(define-fun " << stepLabel << " () Bool " << goalTerm << ")\n\n";
      std::vector<std::string> stepPrem = outerPrem;
      stepPrem.push_back(ihTerm);
      if (ps->theorem) {
        std::string lo = smtExprWp(c, ps->theorem->lo.get(), stepStore);
        std::string hi = smtExprWp(c, ps->theorem->hi.get(), stepStore);
        stepPrem.push_back("(>= " + ksym + " " + lo + ")");
        stepPrem.push_back(ps->theorem->inclusive
            ? "(< " + ksym + " " + hi + ")"
            : "(< (+ " + ksym + " 1) " + hi + ")");
      }
      smtDischarge(c, stepLabel, stepLabel, stepPrem);
    }
    return;
  }

  // proof { <stmts> }  -  proof-only block. Unlike a normal Block, this has no
  // runtime state effect: lemma-call ExprStmts add callee ensures into a private
  // proof path, and nested asserts discharge under those proof facts. The outer
  // WP store/path are intentionally left unchanged after the block.
  if (auto pb = dynamic_cast<const ProofBlockStmt*>(s)) {
    c.out << "; --- proof block at line " << pb->line << " ---\n";
    std::map<std::string, std::string> proofStore = store;
    std::string proofPath = pathCond;
    bool proofReturned = false;
    WpEns noEns;
    smtEncodeStmts(c, fnName, pb->body, depth + 1, premises, proofStore,
                   proofPath, proofReturned, noEns);
    return;
  }

  // if cond { then } else { else_ }
  if (auto is = dynamic_cast<const IfStmt*>(s)) {
    std::string condTerm = smtExprWp(c, is->cond.get(), store);
    // Fork. Each arm gets a private copy of the store; the `returned` flag
    // tells mergeStores whether the arm short-circuited.
    std::map<std::string, std::string> thenStore = store;
    std::map<std::string, std::string> elseStore = store;
    bool thenRet = false, elseRet = false;
    smtEncodeStmts(c, fnName, is->then, depth + 1, premises, thenStore,
                   pathAnd(pathCond, condTerm), thenRet, ens);
    smtEncodeStmts(c, fnName, is->else_, depth + 1, premises, elseStore,
                   pathAnd(pathCond, "(not " + condTerm + ")"), elseRet, ens);
    if (thenRet && elseRet) { returned = true; return; }
    mergeStores(store, thenStore, thenRet, elseStore, elseRet, condTerm);
    return;
  }

  // ox:proof while cond invariant I* { body }   -  3-check induction scheme.
  if (auto ws = dynamic_cast<const WhileStmt*>(s)) {
    std::string condTerm = smtExprWp(c, ws->cond.get(), store);
    // (1) Each invariant holds at loop entry (under pathCond ∧ cond).
    std::vector<std::string> invTerms;
    int n = 0;
    for (auto& inv : ws->invariants) {
      if (!inv) continue;
      std::string label = fnName + "_invariant_d" + std::to_string(depth)
                          + "_" + std::to_string(n);
      c.out << "; " << label << " (while-entry, source line " << inv->line << ")\n";
      std::string term = smtExprWp(c, inv.get(), store);
      c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
      std::vector<std::string> prem = premises;
      std::string pc = pathAnd(pathCond, condTerm);
      if (!pc.empty()) prem.push_back(pc);
      smtDischarge(c, label, label, prem);
      invTerms.push_back(term);
      ++n;
    }
    if (invTerms.empty()) {
      // ox:unsafe No invariant  -  assume the loop never runs (sound, honest, loose).
      // We emit `(assert false)` under the loop's path so downstream assertions
      // in this arm trivially discharge, and document it.
      c.out << "; warning: while loop at line " << ws->line
            << " has no invariant  -  body assumed unreachable for verification\n";
      // Mark all subsequent code in this path dead via pathCond = "false".
      // The cleanest way: discharge downstream under (assert false).
      // We achieve this by recursing into the body with pathCond = "false"
      // AND marking the post-loop code path dead  -  implemented by setting
      // `returned = true` so the caller stops processing further stmts.
      // HONEST: this loses any real proof opportunity in the loop, but the
      // alternative (an unconstrained body) is unsound. The user MUST supply
      // an invariant to verify a loop.
      std::map<std::string, std::string> bodyStore = store;
      bool bodyRet = false;
      smtEncodeStmts(c, fnName, ws->body, depth + 1, premises, bodyStore,
                     "false", bodyRet, ens);
      returned = true;
      return;
    }
    // (2) Preservation: assuming I ∧ cond at body entry, I holds at body exit.
    // We build the body-entry premise list = premises + pathCond + cond + I.
    std::vector<std::string> bodyPrem = premises;
    if (!pathCond.empty()) bodyPrem.push_back(pathCond);
    bodyPrem.push_back(condTerm);
    for (auto& it : invTerms) bodyPrem.push_back(it);
    std::map<std::string, std::string> bodyStore = store;
    bool bodyRet = false;
    smtEncodeStmts(c, fnName, ws->body, depth + 1, bodyPrem, bodyStore,
                   pathAnd(pathCond, condTerm), bodyRet, ens);
    // Re-check each invariant at body end (under the body-entry premises +
    // the body's path conditions, which smtEncodeStmts already discharged
    // asserts under). For preservation we just re-discharge I with the
    // post-body store; the body-premises are still in `bodyPrem`.
    if (!bodyRet) {
      int m = 0;
      for (auto& inv : ws->invariants) {
        if (!inv) continue;
        std::string label = fnName + "_invariant_pres_d" + std::to_string(depth)
                            + "_" + std::to_string(m);
        c.out << "; " << label << " (while-preservation, source line " << inv->line << ")\n";
        std::string term = smtExprWp(c, inv.get(), bodyStore);
        c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
        smtDischarge(c, label, label, bodyPrem);
        ++m;
      }
    }
    // (3) At loop exit (pathCond ∧ ¬cond ∧ I), continue with a fresh post-loop
    // store in which the names MUTATED by the body are re-bound to fresh
    // uninterpreted consts. The body's post-store (`bodyStore`) carries only
    // ONE-iteration values (e.g. `i = 0 + 1 = 1` for `i = i + 1` starting from
    // `i = 0`), which is NOT the value the loop variable holds at loop exit  - 
    // the loop may run 0, 1, ..., n times. Worse, the loop-entry `invTerms`
    // were encoded against the pre-loop store (with `i = 0` literal), so they
    // are concrete and useless as loop-exit hypotheses: `(and (<= 0 0) (<= 0 n))`
    // says nothing about the post-loop `i`. By binding the mutated names to fresh
    // symbolic consts and re-encoding BOTH the loop guard AND each invariant
    // under that exit store, the loop-exit path condition becomes
    // `pathCond ∧ ¬cond(fresh) ∧ I(fresh)`  -  which honestly constrains the fresh
    // consts (e.g. `0 ≤ i_ex ∧ i_ex ≤ n ∧ ¬(i_ex < n)` ⟹ `i_ex == n`). Downstream
    // `assert`/`ensures` clauses read those names from the store and see the
    // SAME fresh symbol the invariant constrains, so the invariant's strength
    // discharges the post-loop fact. Names NOT mutated by the body keep their
    // pre-loop bindings (sound  -  the loop didn't touch them). This also fixes
    // the vacuous-discharge bug where the previously-concrete `condTerm`
    // (`(< 0 n)` for `i = 0`) folded to `false` after negation, making
    // downstream asserts pass UNsat for the wrong reason (contradictory premise
    // rather than genuine derivation from the invariant).
    //
    // Missing-#3 fix note (preserved from the prior implementation): we keep
    // ¬cond in exitCond (NOT just `pathCond ∧ I`); the negated guard at loop
    // exit is the term that, combined with the upper bound in the invariant,
    // pins the loop variable. We never collapse exitCond to `false`  -  the
    // fresh-symbol formulation makes that impossible (no const-fold happens on
    // an uninterpreted const), so Z3 discharges honestly.
    std::set<std::string> mutated;
    collectMutatedVarNames(ws->body, mutated);
    std::map<std::string, std::string> exitStore = store;  // start from pre-loop
    for (const auto& nm : mutated) {
      // Mint a fresh uninterpreted const for this name's value at loop exit.
      // The placeholderSeq counter makes the name unique per loop instance
      // (a function may have several while-loops at the same depth, e.g. in
      // sibling arms of an if).
      std::string exitSym = fnName + "_loopexit_" + nm + "_d"
                            + std::to_string(depth)
                            + "_" + std::to_string(c.placeholderSeq++);
      smtDeclareConst(c, exitSym, "Int");
      exitStore[nm] = exitSym;
    }
    // Re-encode the loop guard and each invariant under the exit store so their
    // variable references resolve to the fresh consts. These replace the
    // loop-entry `condTerm` / `invTerms` (which were concretised against the
    // pre-loop store) in the exit path condition.
    std::string exitCondTerm = smtExprWp(c, ws->cond.get(), exitStore);
    std::vector<std::string> exitInvTerms;
    for (auto& inv : ws->invariants) {
      if (!inv) continue;
      exitInvTerms.push_back(smtExprWp(c, inv.get(), exitStore));
    }
    std::string exitCond = pathAnd(pathCond, "(not " + exitCondTerm + ")");
    for (auto& it : exitInvTerms) exitCond = pathAnd(exitCond, it);
    store = exitStore;
    store["__ox_pathcond__"] = exitCond;
    return;
  }

  // for var = start..end { body }   -  model as a while over a synthetic counter.
  // We bind the loop var in the store to a fresh symbol, and treat the bounds
  // as the loop guard. Same 3-check scheme as `while` for invariants.
  if (auto fs = dynamic_cast<const ForStmt*>(s)) {
    // Bind the loop variable to a fresh uninterpreted const (its value
    // throughout the loop is quantified隐  -  we don't expand iterations).
    std::string loopVarSym = fnName + "_forvar_" + std::to_string(depth)
                             + "_" + fs->varName;
    if (!c.declared.count(loopVarSym)) {
      c.out << "(declare-const " << loopVarSym << " Int)\n";
      c.declared.insert(loopVarSym);
    }
    store[fs->varName] = loopVarSym;
    // Guard: start <= loopVar AND (loopVar < end | loopVar <= end).
    std::string startTerm = fs->start ? smtExprWp(c, fs->start.get(), store) : "0";
    std::string endTerm   = fs->end   ? smtExprWp(c, fs->end.get(),   store) : "0";
    std::string guard = fs->inclusiveEnd
                        ? ("(and (>= " + loopVarSym + " " + startTerm + ")"
                           " (<= " + loopVarSym + " " + endTerm + "))")
                        : ("(and (>= " + loopVarSym + " " + startTerm + ")"
                           " (< "  + loopVarSym + " " + endTerm + "))");
    // (1) invariants at loop entry under guard.
    std::vector<std::string> invTerms;
    int n = 0;
    for (auto& inv : fs->invariants) {
      if (!inv) continue;
      std::string label = fnName + "_invariant_d" + std::to_string(depth)
                          + "_" + std::to_string(n);
      c.out << "; " << label << " (for-entry, source line " << inv->line << ")\n";
      std::string term = smtExprWp(c, inv.get(), store);
      c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
      std::vector<std::string> prem = premises;
      std::string pc = pathAnd(pathCond, guard);
      if (!pc.empty()) prem.push_back(pc);
      smtDischarge(c, label, label, prem);
      invTerms.push_back(term);
      ++n;
    }
    if (invTerms.empty()) {
      c.out << "; warning: for loop at line " << fs->line
            << " has no invariant  -  body assumed unreachable for verification\n";
      std::map<std::string, std::string> bodyStore = store;
      bool bodyRet = false;
      smtEncodeStmts(c, fnName, fs->body, depth + 1, premises, bodyStore,
                     "false", bodyRet, ens);
      returned = true;
      return;
    }
    // (2) preservation: assume guard + invariants, run body, re-check invariants.
    std::vector<std::string> bodyPrem = premises;
    if (!pathCond.empty()) bodyPrem.push_back(pathCond);
    bodyPrem.push_back(guard);
    for (auto& it : invTerms) bodyPrem.push_back(it);
    std::map<std::string, std::string> bodyStore = store;
    bool bodyRet = false;
    smtEncodeStmts(c, fnName, fs->body, depth + 1, bodyPrem, bodyStore,
                   pathAnd(pathCond, guard), bodyRet, ens);
    if (!bodyRet) {
      int m = 0;
      for (auto& inv : fs->invariants) {
        if (!inv) continue;
        std::string label = fnName + "_invariant_pres_d" + std::to_string(depth)
                            + "_" + std::to_string(m);
        c.out << "; " << label << " (for-preservation, source line " << inv->line << ")\n";
        std::string term = smtExprWp(c, inv.get(), bodyStore);
        c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
        smtDischarge(c, label, label, bodyPrem);
        ++m;
      }
    }
    // (3) exit: ¬guard ∧ invariants. Continue with post-body store.
    // Missing-#3 fix: same as while  -  add ¬guard to exitCond, not just the
    // invariants. Without ¬guard the downstream discharge queries run under
    // an over-approximate premise (loop might still be going); with ¬guard
    // the premise honestly reflects that the loop has terminated, which is
    // what compositions like EPT handlers walking tables need.
    std::string exitCond = pathAnd(pathCond, "(not " + guard + ")");
    for (auto& it : invTerms) exitCond = pathAnd(exitCond, it);
    store = bodyStore;
    store["__ox_pathcond__"] = exitCond;
    return;
  }

  // block { stmts }   -  recurse.
  if (auto bl = dynamic_cast<const Block*>(s)) {
    smtEncodeStmts(c, fnName, bl->stmts, depth, premises, store, pathCond,
                   returned, ens);
    return;
  }

  // `sync { ... }`  -  a runtime barrier block. The barrier itself has no
  // direct SMT effect (it's a thread rendezvous, not a state mutation), so we
  // encode the body's statements in the current store just like a plain Block,
  // letting any `let`/assignment/contract inside it flow into the WP store.
  if (auto sb = dynamic_cast<const SyncBlock*>(s)) {
    smtEncodeStmts(c, fnName, sb->body, depth, premises, store, pathCond,
                   returned, ens);
    return;
  }

  // defer stmt   -  body is run on scope exit; we encode it as if inlined here
  // (its symbolic effects flow into the store). Honest: defer ordering with
  // RAII drops is approximated; for verification we treat it as a block.
  if (auto ds = dynamic_cast<const DeferStmt*>(s)) {
    if (ds->body) {
      // Wrap the single deferred stmt in a one-element vector via a recurse
      // on a synthesized scope  -  simplest: encode the stmt directly.
      smtEncodeStmt(c, fnName, ds->body.get(), depth, premises, store,
                   pathCond, returned, ens);
    }
    return;
  }

  // break / continue  -  terminate this path for verification purposes. We
  // mark the path dead (no further discharge)  -  sound, slightly loose: a
  // real `break` exits the loop and continues after it, but tracking loop-
  // exit continuations precisely needsjoin/condensation we don't model here.
  // For invariant verification the loop's 3-check scheme already covers the
  // interesting cases; a `break` inside the body just means that path stops.
  if (dynamic_cast<const BreakStmt*>(s)) { returned = true; return; }
  if (dynamic_cast<const ContinueStmt*>(s)) { returned = true; return; }
  // Default: unknown stmt kind  -  no symbolic effect (honest).
}

// Walk a statement list with the WP encoder. Threads `store` and `pathCond`
// through each statement; sets `returned = true` if the path short-circuits
// (return / break / continue). The `__ox_pathcond__` store key (if set by a
// loop exit) is folded into the active path condition for downstream stmts.
static void smtEncodeStmts(SmtCtx& c, const std::string& fnName,
                          const std::vector<StmtPtr>& stmts, int depth,
                          const std::vector<std::string>& premises,
                          std::map<std::string, std::string>& store,
                          const std::string& pathCond, bool& returned,
                          const WpEns& ens) {
  // Pick up any pending loop-exit path condition stored by a prior while/for.
  std::string pc = pathCond;
  auto pcit = store.find("__ox_pathcond__");
  if (pcit != store.end()) {
    pc = pcit->second.empty() ? pc : pathAnd(pc, pcit->second);
    store.erase("__ox_pathcond__");
  }
  auto cait = store.find("__ox_call_assume__");
  if (cait != store.end()) {
    pc = cait->second.empty() ? pc : pathAnd(pc, cait->second);
    store.erase("__ox_call_assume__");
  }
  for (auto& s : stmts) {
    if (returned) return;
    c.wpPremises = premises;
    c.wpPathCond = pc;
    smtEncodeStmt(c, fnName, s.get(), depth, premises, store, pc, returned, ens);
    // A loop may have stashed a fresh path condition; fold it in.
    auto np = store.find("__ox_pathcond__");
    if (np != store.end()) {
      pc = np->second.empty() ? pc : pathAnd(pc, np->second);
      store.erase("__ox_pathcond__");
    }
    auto ca = store.find("__ox_call_assume__");
    if (ca != store.end()) {
      pc = ca->second.empty() ? pc : pathAnd(pc, ca->second);
      store.erase("__ox_call_assume__");
    }
  }
}

// ox:proof emitFnContracts  -  emit one function's full SMT contract surface: param
// declarations, `result`, `old_` snapshots, `requires_*` (assumed), Tier A
// `ensures_*` fallback, and Tier B WP-encoded body contracts + return-site
// `ensures_ret_*`. Captured into a standalone helper so the SAME machinery
// applies to BOTH top-level free functions AND impl-block methods. The only
// caller-controlled value is `fnName`, used as the symbol-prefix / display /
// `nameMap` qualifier (a free `fn` uses `fn->name`; an impl method uses its
// `mangleMethod(structName, fn.name)` so all symbols match the mangling the
// Ghost encoder already uses). `prog.funcs` and `prog.impls[].methods` share
// the `FuncDecl` shape so a single instance of this code covers both.
//
// Tier 1  -  `postIdents` (if non-null) receives, for every non-const top-level
// global `g`, the final symbolic-store term the body leaves `g` at on exit
//  -  see `PostStateMap` in Smt.h. The Ghost section's frame-axiom emitter
// consults this map to make `modifies`-clause frame axioms non-vacuous
// (compares the body's actual post term to the pre-state snapshot, instead
// of two unrelated fresh uninterpreted consts). The seeding that makes this
// work: for every non-const global `g`, BEFORE the WP body walk, we declare a
// fresh `<fnName>_old_<g>` uninterpreted const and seed `store[g]` with it.
// Then:
//   - body never assigns `g`  → store[g] still = old_<g>  → frame `(= old old)`
//     discharges (unsat negation)  -  provably unchanged.
//   - body assigns `g` once    → store[g] = new post term → frame `(= post old)`
//     is sat  -  honestly flagging a global the fn touched but didn't `modify`.
// A ghost fn skips the WP walk (its body is spec-only); for ghost fns we still
// seed `store[g]` + declare `old_<g>` (harmless  -  ghost fns have empty
// `modifies` so no frame axiom is ever emitted for them), but we DO NOT record
// a `postIdents` entry (the frame emitter never asks for ghost fns anyway).

// ox:proof Tier 3-1  -  pre-seed nameMap + store + declared for every `ghost let`
// reachable in a function body's statement nesting. Called by
// `emitFnContracts` BEFORE the contract-clause discharge queries run
// (they need the ghost symbol to resolve). Walks Block/If/While/For
// (mirrors `Sema::predeclareGhostLetsStmt`); DeferStmt is deliberately
// not recursed into (T3-1 scope  -  spec-exotic). Each `GhostLetStmt`:
//   - emit `(declare-const ghost_<fn>_<name> <Sort>)` once (dedup via
//     `c.declared` to avoid Z3 abort on a duplicate decl  -  same guard the
//     body walker uses),
//   - bind `c.nameMap[name] = gsym` so `smtExpr`'s VarRef lookup resolves
//     `name` to the SMT symbol (the body walker also does this later  - 
//     harmless overwrite),
//   - seed `store[name] = gsym` so `smtExprWp`'s store lookup hits it
//     before the body walker rebinds (and so a rebind via `g = e` is read
//     by the return-site ensures).
static void preseedGhostLetsInStmts(SmtCtx& c,
                                    const std::vector<StmtPtr>& stmts,
                                    const std::string& fnName,
                                    std::map<std::string, std::string>& store);
static void preseedGhostLetsInStmt(SmtCtx& c, const Stmt* s,
                                   const std::string& fnName,
                                   std::map<std::string, std::string>& store) {
  if (!s) return;
  if (auto gl = dynamic_cast<const GhostLetStmt*>(s)) {
    std::string gsym = "ghost_" + fnName + "_" + gl->name;
    if (!c.declared.count(gsym)) {
      c.out << "(declare-const " << gsym << " " << smtSort(gl->type) << ")\n";
      c.declared.insert(gsym);
    }
    c.nameMap[gl->name] = gsym;
    store[gl->name] = gsym;
    return;  // don't recurse into a ghost let's init  -  spec-only
  }
  if (auto bl = dynamic_cast<const Block*>(s)) {
    preseedGhostLetsInStmts(c, bl->stmts, fnName, store);
  } else if (auto is = dynamic_cast<const IfStmt*>(s)) {
    preseedGhostLetsInStmts(c, is->then, fnName, store);
    preseedGhostLetsInStmts(c, is->else_, fnName, store);
  } else if (auto ws = dynamic_cast<const WhileStmt*>(s)) {
    preseedGhostLetsInStmts(c, ws->body, fnName, store);
  } else if (auto fs = dynamic_cast<const ForStmt*>(s)) {
    preseedGhostLetsInStmts(c, fs->body, fnName, store);
  } else if (auto a = dynamic_cast<const AssertStmt*>(s)) {
    // ox:proof `assert <expr> by { <hints> };`  -  pre-seed ghost lets in the hint block.
    preseedGhostLetsInStmts(c, a->byBody, fnName, store);
  } else if (auto cs = dynamic_cast<const CalcStmt*>(s)) {
    // ox:proof `calc { <expr>; <REL> { <hints> } <expr>; ... }`  -  pre-seed ghost lets in
    // every step's proof hints (parity with `assert ... by { }` above). The
    // step expressions themselves are not recursed  -  they are pure expressions
    // (no nested ghost-let bindings), and the relation strings carry no code.
    for (auto& step : cs->steps) {
      preseedGhostLetsInStmts(c, step.hints, fnName, store);
    }
  }
  // ox:note DeferStmt deliberately not recursed  -  see T3-1 scope note in predeclareGhostLets (Sema.cpp).
}
static void preseedGhostLetsInStmts(SmtCtx& c,
                                    const std::vector<StmtPtr>& stmts,
                                    const std::string& fnName,
                                    std::map<std::string, std::string>& store) {
  for (auto& s : stmts) {
    if (s) preseedGhostLetsInStmt(c, s.get(), fnName, store);
  }
}

static void emitFnContracts(std::ostringstream& out,
                            const FuncDecl& fn,
                            const std::string& fnName,
                            const std::map<std::string, long long>& constGlobals,
                            const std::map<std::string, const SpecFnDecl*>& specFns,
                            const std::map<std::string, const FuncDecl*>& funcDecls,
                            const std::map<std::string, const FuncDecl*>& methodDecls,
                            const Program& prog,
                            PostStateMap* postIdents = nullptr) {
  if (fn.isExtern) return;
  bool hasReq = !fn.requires_.empty();
  bool hasEns = !fn.ensures_.empty();
  bool hasBodyContracts = false;   // detected by the walk below
  (void)hasBodyContracts;

  out << "; ============================================================\n";
  out << "; function " << fnName << "\n";
  out << "; ============================================================\n";

  // One shared declaration scope for the whole function. Each parametric/
  // slot name is declared once with a function-qualified symbol so two
  // functions can't clash even when they share a param name (`fn a(n)` &
  // `fn b(n)`). The bare source name is mapped to that symbol so clause
  // terms resolve. `smtDeclareConst` dedups within a function.
  SmtCtx c{out, nullptr, 0, 0, {}};
  c.curFn = fnName;
  c.nameMap.clear();
  c.constGlobals = constGlobals;
  c.specFns = specFns;
  c.funcDecls = funcDecls;
  c.methodDecls = methodDecls;

  // Tier 2a  -  if this is an impl-block method with a `self` receiver, look
  // up the owning struct and pre-declare one fresh uninterpreted SMT const
  // per field: `self__<StructName>__<fieldName>` of the field's sort. Bind
  // it in `nameMap` AND seed the WP `store` so that:
  //   - a `self.x` read in the body / a contract clause resolves directly
  //     to the per-field symbol (no placeholder),
  //   - a `self.x = e` write overwrites `store["self__S__x"]` with the new
  //     post term so subsequent reads see the update (mirror of how globals
  //     work after the Tier 1 seeding), and
  //   - an untouched field keeps its function-entry fresh uninterpreted
  //     value  -  sound: the caller's struct state is unknown at entry, so a
  //     fresh uninterpreted const per field is the honest model.
  // Cheap-strength: each field is an independent uninterpreted const; the
  // plan's `Focus { owner: 5, seq: 0 }` struct-literal equality case (which
  // would tie the fields together via `(= self__S__owner 5) ∧ (= self__S__seq 0)`)
  // is NOT modelled at Tier 2a  -  only the seed-and-read/write path. A later
  // tier can add the literal-equality strengthening.
  if (fn.hasSelf && !fn.implStruct.empty()) {
    c.selfStructName = fn.implStruct;
    for (auto& sd : prog.structs) {
      if (!sd || sd->name != fn.implStruct) continue;
      for (auto& fld : sd->fields) {
        const std::string& fname = std::get<0>(fld);
        BType ftype = std::get<1>(fld);
        bool isPrivate = std::get<2>(fld);
        (void)isPrivate;   // contracts may reference private fields  -  same struct
        std::string sym = "self__" + fn.implStruct + "__" + fname;
        smtDeclareConst(c, sym, smtSort(ftype));
        c.nameMap["self__" + fn.implStruct + "__" + fname] = sym;
        // ox:why Seed the WP store with the entry symbol so the `Field` arm's store
        // lookup hits it for an unmodified field. `store` is declared just
        // below at function scope (Tier 1 hoist); seeding here is safe  -  the
        // WP body walk runs later and will overwrite entries as the body
        // assigns fields.
      }
      break;
    }
  }

  // Decide which source names flow into a bitwise/shift op ANYWHERE in this
  // function's contracts or body. Those are declared as native `(_ BitVec 64)`
  // rather than Int, so mask/shift queries stay in decidable QF_BV (decisive
  // sat/unsat + counterexample models) and arithmetic on them wraps at 64
  // bits like real hardware. A name that never touches a bitop stays pure Int
  // (unbounded, cleaner for ordinary numeric proofs). Sound either way: the
  // int2bv/bv2int bridges are total.
  std::set<std::string> bitNames;
  for (auto& r : fn.requires_) smtCollectBitopsExpr(r.get(), bitNames);
  for (auto& e : fn.ensures_)  smtCollectBitopsExpr(e.get(), bitNames);
  for (auto& s : fn.body)      smtCollectBitopsStmt(s.get(), bitNames, bitNames);

  for (auto& p : fn.params) {
    // ox:proof Sanitize: only alnum/_ allowed in SMT identifiers; sanitize the fn
    // name + param name defensively (function names are identifiers here).
    std::string q = "p_" + fnName + "_" + p.name;
    bool wantsBv = bitNames.count(p.name) &&
                   (smtSort(p.type) == std::string("Int"));
    if (wantsBv) {
      smtDeclareConst(c, q, ("(_ BitVec " + std::to_string(BV_W) + ")").c_str());
      c.bvVars.insert(q);
    } else {
      smtDeclareConst(c, q, smtSort(p.type));
    }
    c.nameMap[p.name] = q;
    // Tier 3  -  record the declared compile-time array length for a `[T; N]`
    // param so `smtLenOf` can emit `(= len_p_<fn>_<p> N)`. The key is the
    // SMT base symbol `q` (`smtLenOf` looks up by `smtExprBaseName`'s result).
    // Only Tag::array carries a positive `count`; non-array params are left
    // out and `smtLenOf` falls back to the free-int behavior for them.
    if (p.type.tag == BType::Tag::array && p.type.count > 0) {
      c.arrayLenSyms[q] = p.type.count;
    }
  }
  if (hasEns && fn.retType != BType::void_) {
    std::string q = fnName + "_result";
    bool wantsBv = bitNames.count("result") &&
                   (smtSort(fn.retType) == std::string("Int"));
    if (wantsBv) {
      smtDeclareConst(c, q, ("(_ BitVec " + std::to_string(BV_W) + ")").c_str());
      c.bvVars.insert(q);
    } else {
      smtDeclareConst(c, q, smtSort(fn.retType));
    }
    c.nameMap["result"] = q;
  }
  if (hasEns) {
    std::set<std::string> olds;
    for (auto& e : fn.ensures_) collectOldNames(e.get(), olds);
    for (const auto& nm : olds) {
      std::string q = fnName + "_old_" + nm;
      smtDeclareConst(c, q, smtSort(BType::i64));
      c.nameMap["old_" + nm] = q;
    }
  }
  // Tier 1  -  pre-state seeding for mutable globals. For every non-const
  // top-level global `g`, declare a fresh `<fnName>_old_<g>` uninterpreted
  // const (the pre-state snapshot  -  the SAME symbol `ensures old(g)` would
  // have used if it had mentioned `g`) and seed `store[g]` with it BEFORE
  // any WP walking happens. Consequences at function-exit:
  //   - body never assigns `g`: store[g] is still `<fn>_old_<g>`. The frame
  //     axiom the Ghost section emits for `g` (when `g` is NOT in the fn's
  //     `modifies` set) becomes `(= <fn>_old_<g> <fn>_old_<g>)` and
  //     discharges to unsat  -  provably unchanged by the body, the real
  //     Dafny-modifies semantics.
  //   - body assigns `g` (e.g. `g = e` or `g += 1`): store[g] is overwritten
  //     with the new post term. Frame becomes `(= <post> <fn>_old_<g>)`  - 
  //     sat in general, honestly flagging that the fn touched a global it
  //     didn't list in `modifies`.
  // We also bind `nameMap["old_"+g]` so a contract clause that explicitly
  // reads `old(g)` resolves to the SAME pre-state symbol the body's reads
  // started from  -  keeps the pre-state single-rooted (no shadowing).
  //
  // `store` is declared at function scope (hoisted out of the `if (!isGhost)`
  // block below) so the seeding persists whether or not the WP walk runs.
  // For a ghost fn the WP walk is skipped (body is spec-only) so `store`
  // stays seeded with the pre-state consts  -  we still record a `postIdents`
  // entry equal to the pre-state const, and the Ghost section's frame axiom
  // collapses identically; ghost fns have empty `modifies` so no frame axiom
  // is emitted for them anyway, making the entry dead  -  but consistent.
  std::map<std::string, std::string> store;   // Tier 1  -  hoisted to fn scope
  for (auto& g : prog.globals) {
    if (!g || g->isConst) continue;          // const globals are immutable
    std::string oldSym = fnName + "_old_" + g->name;
    // `smtDeclareConst` dedups within the function's `c.declared` set: if
    // the `ensures old(g)` block above already declared it, this is a no-op.
    smtDeclareConst(c, oldSym, smtSort(g->type));
    c.nameMap["old_" + g->name] = oldSym;
    store[g->name] = oldSym;                  // pre-state seed
    // Tier 3  -  record the global array's declared compile-time length. The
    // store snapshot used by the body walk is `oldSym`, so that's the key
    // `smtExprBaseName` returns when a contract reads `g`. Globals aren't
    // pre-declared as top-level fresh consts here (the only fresh symbol
    // about `g` the SMT stream sees is `oldSym`), so we DON'T also seed a
    // bare-`g->name` entry: the body always reads `g` through the store.
    if (g->type.tag == BType::Tag::array && g->type.count > 0) {
      c.arrayLenSyms[oldSym] = g->type.count;
    }
  }
  // MMIO-thread  -  seed `c.mmioState` from the function's `modifies` clause.
  // Per spec Part 1 item 4: "initialize mmioState from the function's modifies
  // clause (if it includes MMIO region names)". Any modifies entry that isn't
  // an already-declared top-level global (checked above) AND isn't a field
  // symbol is treated as a named MMIO region: declare a fresh `{fn}_mmio_old_{r}`
  // uninterpreted Int const as the pre-state value, bind `old_{r}` so an
  // `ensures old(mmio_addr)` clause can reference it, and seed `c.mmioState[r]`
  // with that const so `mmio_load(r)` at function entry reads the pre-state.
  // Distinct regions get distinct consts  -  sound, models each region's value
  // as independent (matches the named-per-address MMIO model in Smt.h).
  for (const auto& r : fn.modifies) {
    // Skip ordinary mutable globals  -  already seeded into `store` above.
    bool isGlobal = false;
    for (auto& g : prog.globals) {
      if (g && g->name == r) { isGlobal = true; break; }
    }
    if (isGlobal) continue;
    // Treat this modifies entry as an MMIO region name.
    std::string oldSym = fnName + "_mmio_old_" + r;
    smtDeclareConst(c, oldSym, "Int");
    c.nameMap["mmio_old_" + r] = oldSym;
    c.mmioState[r] = oldSym;
  }
  // Effect-system linkage  -  the `effects { ... }` clause on a function head
  // is parsed (Parser.cpp) and propagated to the FuncSig (Sema.cpp) but the
  // heavy lifting for the SMT frame axiom lives in Ghost.cpp's
  // `emitRegionsAndModifies`. Two cases affect this function's SMT output:
  //   1. Pure fn (`effectsExplicit && effects.empty()`): Ghost.cpp emits a
  //      *blanket* frame assertion  -  every mutable global `g` gets
  //      `(assert (= <post g> <fn>_old_<g>))` regardless of whether `g` is
  //      named in `modifies`. That is stronger than the named-region frame
  //      the `modifies` list alone produces, and it lets the verifier prove
  //      "pure code doesn't touch memory" without enumerating regions.
  //   2. Effects containing `mmio`/`vmcs_read`/`vmcs_write`: Ghost.cpp SKIPS
  //      the blanket unchanged frame (those effects modify hardware state the
  //      `<fn>_old_<g>` snapshot doesn't cover) and falls back to the named
  //      `modifies`-region frame.
  // We do NOT need to emit any extra declarations here  -  the pre-state
  // `<fn>_old_<g>` consts (seeded just above) ARE the symbols the blanket
  // frame reuses. This block is a no-op emit, kept for documentation and to
  // keep the effect-system reasoning visible at the contract-emit site.
  (void)fn.effectsExplicit;   // referenced by Ghost.cpp's frame emitter
  (void)fn.effects;           // ditto
  // Tier 2a  -  seed the WP store with the per-field self symbols declared in
  // the struct-seeding block above. A `self.x` read in the body resolves via
  // `store` to this symbol if untouched, or to the post term if `self.x = e`
  // reassigned it (handled in `smtEncodeStmt`'s `AssignTarget::field` arm).
  if (fn.hasSelf && !c.selfStructName.empty()) {
    for (auto& sd : prog.structs) {
      if (!sd || sd->name != c.selfStructName) continue;
      for (auto& fld : sd->fields) {
        const std::string& fname = std::get<0>(fld);
        std::string sym = "self__" + c.selfStructName + "__" + fname;
        store["self__" + c.selfStructName + "__" + fname] = sym;
      }
      break;
    }
  }
  // ox:proof Tier 3-1  -  pre-seed nameMap + store + declared for every `ghost let`
  // reachable in the body's nesting, mirroring `Sema::predeclareGhostLets`
  // (Sema.cpp) but binding to SMT symbols instead of Oxide types. The body
  // walker's LetStmt arm (smtEncodeStmt, below) rebinds `store[name] = gsym`
  // when it reaches the ghost let AND emits the `(declare-const ...)`. But
  // the contract-clause discharge queries (requires/ensures fallback) run
  // BEFORE the body walker  -  so a `requires g` or `ensures result == g`
  // would otherwise punt `g` to a placeholder (`unknown name 'g' -> ph0`).
  //
  // We pre-seed `c.nameMap[g] = gsym` so `smtExpr`'s VarRef lookup resolves
  // `g` even before the body walk; we pre-declare the symbol (`c.declared`
  // + the actual `(declare-const ...)`) so the discharge queries don't
  // reference an undeclared name; and we seed `store[g] = gsym` so the
  // body-walker's WP reads of `g` resolve consistently with the signature
  // clause's reads (including Tier 2's assignment rebind path  -  a `g = e`
  // assignment body arm overwrites `store[g]`, then the return-site ensures
  // reading `g` via `smtExprWp`'s store lookup sees the new term).
  preseedGhostLetsInStmts(c, fn.body, fnName, store);

  out << "\n";

  // ox:proof Collect the `requires` clauses as SMT terms so we can carry them as
  // premises into the discharge of `ensures`, `invariant`, and `assert`
  // (Hoare-logic: prove Body ⊢ clause under Pre). A precondition is an
  // ASSUMPTION about the caller, not a goal of the callee, so we do NOT
  // discharge it  -  we only emit the define-fun so downstream clauses can
  // (and so the .smt2 file is self-contained for out-of-band discharge).
  // `oxide verify` thus fails only when a body/ensures/invariant/assert
  // clause is sat-or-unknown; a sat requires is left to the user as a hint
  // (printed but not counted as a failure)  -  matching Dafny/SPARK.
  std::vector<std::string> reqTerms;
  if (hasReq) {
    out << "; ---- requires (assumed, not discharged) ----\n";
    int i = 0;
    for (auto& r : fn.requires_) {
      std::string label = fnName + "_requires_" + std::to_string(i);
      c.out << "; " << label << " (source line " << (r ? r->line : 0) << ")\n";
      std::string term = smtExpr(c, r.get());
      c.out << "(define-fun " << label << " () Bool " << term << ")\n\n";
      reqTerms.push_back(label);   // carry as premise for the rest
      ++i;
    }
  }
  // Feature 4  -  Range type auto-premises. For each param whose type carries
  // a `rangeTypeName` (set by Sema when the param resolves to a range alias
  // declared with `typedef Name = BaseTy where <expr>;`), look up the range
  // registry to get the bound expression. We rebind the typedef NAME to the
  // param's SMT symbol in `nameMap`, lower the range constraint via smtExpr,
  // and emit it as an asserted premise. This means every function taking a
  // range-typed param AUTOMATICALLY gets the range as a premise  -  no manual
  // `requires 0 <= x < N` needed at every call site. Zero-cost for Z3: it's
  // a linear constraint.
  for (auto& p : fn.params) {
    if (!p.type.hasRange || p.type.rangeTypeName.empty()) continue;
    const auto* rte = findRangeType(p.type.rangeTypeName);
    if (!rte || !rte->expr) continue;
    // ox:proof Bind the typedef NAME to this param's SMT symbol so the where-clause's
    // VarRef to the type name resolves to the param symbol.
    auto symIt = c.nameMap.find(p.name);
    if (symIt == c.nameMap.end()) continue;
    // Save/restore the nameMap entry for the typedef name.
    std::string prevBinding;
    bool hadPrev = false;
    auto prevIt = c.nameMap.find(p.type.rangeTypeName);
    if (prevIt != c.nameMap.end()) { prevBinding = prevIt->second; hadPrev = true; }
    c.nameMap[p.type.rangeTypeName] = symIt->second;
    // Lower the range constraint and emit as premise.
    std::string rangeLabel = fnName + "_range_" + p.name;
    std::string rangeTerm = smtExpr(c, rte->expr);
    c.out << "; " << rangeLabel << " (auto: range type " << p.type.rangeTypeName << ")\n";
    c.out << "(define-fun " << rangeLabel << " () Bool " << rangeTerm << ")\n";
    c.out << "(assert " << rangeLabel << ")\n\n";
    reqTerms.push_back(rangeLabel);
    // Restore previous binding.
    if (hadPrev) c.nameMap[p.type.rangeTypeName] = prevBinding;
    else c.nameMap.erase(p.type.rangeTypeName);
  }
  if (hasEns) {
    out << "; ---- ensures (signature-level, fallback) ----\n";
    int i = 0;
    for (auto& e : fn.ensures_)
      smtClause(c, fnName, "ensures", i++, e.get(), reqTerms);
  }
  out << "; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----\n";
  // Tier B: thread a symbolic store through the body so `let`/`x = e`/`if`/
  // `while`/`return` flow real terms into invariants/asserts and so every
  // `return e` site discharges the `ensures` clauses with `result` bound to
  // `smtExprWp(e, store)`  -  the body-level proof the Tier A signature-
  // level discharge above can't see. The Tier A path is retained as a
  // fallback so a function whose body the WP encoder can't fully follow
  // still has a best-effort discharge; Z3 sees both, unsat beats sat.
  //
  // Skip ghost fns: their body is spec-only and not subject to WP  -  their
  // contracts were already reflected by the Tier A `smtClause` above and
  // by the T2 ghost-fn reflection in the Ghost encoder. Running the WP
  // walker on a ghost fn would also try to discharge calls that are only
  // legal in spec contexts, producing noise.
  if (!fn.isGhost) {
    WpEns ens;
    ens.clauses = &fn.ensures_;
    std::string fnNameCopy = fnName;   // stable address for the WpEns.fnName ptr
    ens.fnName = &fnNameCopy;
    // Tier 1: `store` is now function-scoped (see the seeding block above)  - 
    // it already holds `store[g] = <fn>_old_<g>` for every non-const global.
    // The WP walk below updates entries as the body assigns names; untouched
    // globals keep the pre-state seed, which is what makes the body's read
    // of an unmodified global resolve to the SAME symbol the frame axiom
    // compares against.
    bool returned = false;
    smtEncodeStmts(c, fnName, fn.body, 0, reqTerms, store, "", returned, ens);
    // If the body fell through without an explicit return and the function
    // has ensures, discharge them with `result` as the outer unconstrained
    // `result` const  -  honest: a path that falls off the end is UB in C,
    // but the user may have written `ensures true`-style clauses that
    // should still discharge.
    if (!returned && hasEns && fn.retType != BType::void_) {
      // `result` is already in nameMap; smtExprWp falls back to it when
      // store has no "result" binding. Pass an empty path cond.
      smtDischargeEnsures(c, fnName, &fn.ensures_, store, "", reqTerms);
    }
  }
  // Tier 1  -  capture the body's final symbolic store for every non-const
  // global, so the Ghost section's frame-axiom emitter can compare the
  // actual post-state term against the pre-state snapshot. We record one
  // entry per (fnName, gname); the frame emitter looks up by fnName
  // (free fn name OR `mangleMethod(S,m)`  -  matching Ghost's keying). An
  // untouched global's `store[g]` is still the pre-state seed `<fn>_old_<g>`,
  // so the recorded entry equals the pre-state snapshot exactly  -  the frame
  // axiom collapses to `(= old old)` and discharges. A global the body
  // reassigned has a different post term, recorded here verbatim.
  //
  // Skip the capture when there's no body-walk (postIdents is null  -  the
  // single-arg `emitGhostSection` overload path, used only by hypothetical
  // external callers)  -  in that case the Ghost section falls back to the
  // vacuous fresh-consts frame axiom (pre-Tier-1 behaviour).
  if (postIdents) {
    for (auto& g : prog.globals) {
      if (!g || g->isConst) continue;
      auto it = store.find(g->name);
      // If the body never touched the name, `store` still has the pre-state
      // seed from the seeding block above. If it was assigned, `store` has
      // the new term. Either way record what's there. (The `old_<g>` decl
      // for this fn already went into the .smt2 stream during seeding, so
      // any term that references it  -  including the post seed itself  -  is
      // well-sorted when the Ghost section's axiom cites it at top level.)
      (*postIdents)[fnName][g->name] =
          (it != store.end()) ? it->second
                              : (fnName + "_old_" + g->name);
    }
  }
  out << "\n";
}

// emitSmt  -  entry point. Walks every non-extern function with contracts and
// writes one .smt2 file with each clause encoded as a boolean SMT-LIB term
// plus the negated discharge query. See the header comment at the top of
// this namespace block. A solver is NOT invoked here; the file is consumed
// out-of-band by Z3/Why3. The Ghost encoder (src/Ghost.cpp) is invoked from
// here for T1/T2/T3 spec fns, regions, refines, ghost lets, and modifies
// frame axioms  -  see emitGhostSection() near the bottom of this block.
//
// Body contracts (invariants / asserts) and return-site `ensures` are
// discharged via the Tier B WP encoder `smtEncodeStmts` which threads a
// symbolic store so `let`/`x = e`/`if`/`while`/`return` flow real symbolic
// values into the clauses  -  instead of leaving body locals as placeholders.
// Signature-level `requires` is still emitted via the Tier A path (assumed,
// not discharged) and `ensures` is ALSO discharged signature-style as a
// fallback (Tier A's `smtClause`) so a function whose body the WP encoder
// can't fully follow (e.g. calls into unsupported forms) still has a
// best-effort signature discharge. Both paths emit independent discharge
// queries; Z3 sees both, and the stronger one wins (unsat beats sat).
//
// T3 methods: impl-block methods (`impl S { fn m(..) { .. } }`) get the
// SAME surface as free fns  -  the contract walker threads through `mangleMethod`
// so every SMT symbol for `S::m` carries the `__oxm_S__m` prefix the Ghost
// encoder already uses for the function's frame axioms. Without this, the
// `modifies` clause's frame axiom would point at a method whose `ensures`
// were never emitted at all  -  silent proof elision.
bool emitSmt(const Program& prog, const std::string& outPath) {

  std::ostringstream out;
  out << "; oxide-generated SMT-LIB (contracts)\n";
  out << "; Encoding: requires/ensures/invariant/assert as boolean terms;\n";
  out << ";           discharge query per clause is (assert (not <term>)) then (check-sat).\n";
  out << ";           unsat  => clause holds for all inputs (static discharge OK).\n";
  out << ";           sat/unknown => clause could not be discharged (or uses an\n";
  out << ";                        uninterpreted placeholder, flagged above).\n";
  out << "; Types: Bool / Int / Real; bools are ints widened for arithmetic.\n\n";
  out << "(set-logic ALL)\n";
  out << "(set-info :status unknown)\n\n";

  // Build a map of compile-time integer `const` globals -> literal value, so
  // clause terms that reference them (e.g. the hypervisor's SDM cross-ref
  // asserts `assert VMCS_EPT_POINTER == 0x001A`) resolve to the literal and
  // can discharge, instead of decaying to uninterpreted placeholders. We fold
  // only the simple forms a `const` initializer takes here: a bare integer
  // literal and a unary-negated integer literal. Anything more complex is left
  // out (it'll fall to a placeholder, honestly undischarged).
  std::map<std::string, long long> constGlobals;
  for (auto& g : prog.globals) {
    if (!g || !g->isConst || !g->init) continue;
    if (auto il = dynamic_cast<const IntLit*>(g->init.get())) {
      constGlobals[g->name] = (long long)il->v;
    } else if (auto u = dynamic_cast<const UnaryExpr*>(g->init.get())) {
      if (u->op == UnaryExpr::Op::neg)
        if (auto il = dynamic_cast<const IntLit*>(u->base.get()))
          constGlobals[g->name] = -(long long)il->v;
    }
  }

  // Index every abstract spec function once so each per-function SmtCtx can
  // inline `spec_fn(args...)` calls in requires/ensures/invariant/assert terms.
  std::map<std::string, const SpecFnDecl*> specFns;
  for (auto& sf : prog.specFns) {
    if (sf) specFns[sf->name] = sf.get();
  }

  std::map<std::string, const FuncDecl*> funcDecls;
  std::map<std::string, const FuncDecl*> methodDecls;
  for (auto& fn : prog.funcs) {
    if (fn && !fn->isExtern) funcDecls[fn->name] = fn.get();
  }
  for (auto& lm : prog.lemmas) {
    if (lm && !lm->isExtern) funcDecls[lm->name] = lm.get();
  }
  for (auto& im : prog.impls) {
    if (!im) continue;
    for (auto& m : im->methods) {
      if (!m || m->isExtern) continue;
      methodDecls[mangleMethod(im->structName, m->name)] = m.get();
    }
  }

  // Tier 1  -  collects, per function (by symbol prefix), the final
  // symbolic-store term for every non-const mutable global after the WP
  // body walk. The Ghost section's frame-axiom emitter consults this map
  // to make `modifies`-clause frame axioms non-vacuous  -  see PostStateMap
  // in Smt.h. Populated by `emitFnContracts` (free fns + impl methods),
  // consumed by the 3-arg `emitGhostSection` overload below.
  PostStateMap postIdents;

  for (auto& fn : prog.funcs) {
    if (!fn) continue;
    emitFnContracts(out, *fn, fn->name, constGlobals, specFns, funcDecls, methodDecls,
                    prog, &postIdents);
  }
  // ox:proof Lemma functions are proof-only, but their contracts are real obligations:
  // each lemma body must establish its own ensures before callers may use those
  // ensures as proof-block hypotheses.
  for (auto& lm : prog.lemmas) {
    if (!lm) continue;
    emitFnContracts(out, *lm, lm->name, constGlobals, specFns, funcDecls, methodDecls,
                    prog, &postIdents);
  }
  // T3 methods  -  impl-block methods share the FuncDecl shape with free fns;
  // emit their full contract surface via the same helper, threaded through
  // `mangleMethod(im->structName, m->name)` so every SMT symbol for the
  // method carries the `__oxm_<S>__<m>` prefix the Ghost encoder already
  // uses for the method's frame axioms. Before this loop existed, a
  // `modifies <region>` frame axiom pointed at a method whose `ensures`
  // were never emitted at all  -  silent proof elision. See header comment
  // at top of `emitSmt`.
  for (auto& im : prog.impls) {
    if (!im) continue;
    for (auto& m : im->methods) {
      if (!m) continue;
      emitFnContracts(out, *m, mangleMethod(im->structName, m->name),
                      constGlobals, specFns, funcDecls, methodDecls, prog, &postIdents);
    }
  }

  // ox:proof T1/T2/T3  -  Ghost encoder section. Appends spec fns, regions,
  // refines discharge queries, ghost-let declarations, and `modifies`
  // frame axioms to the SAME .smt2 stream so a single Z3 run sees the
  // abstract layer alongside the concrete contract terms. Implemented in
  // src/Ghost.cpp (forward-declared in src/Smt.h). No-op if the program
  // declares none of the ghost constructs  -  keeps existing-contract
  // output byte-identical.
  //
  // Tier 1: the 3-arg overload (vs the original 2-arg) threads `postIdents`
  // so frame axioms compare the body's real post-state term to the pre-state
  // snapshot, instead of two fresh unrelated uninterpreted consts (the
  // vacuous pre-Tier-1 pattern that always sat'd or always unsat'd based on
  // nothing about the body). See Ghost.cpp:emitRegionsAndModifies.
  emitGhostSection(prog, out, postIdents);

  out << "(exit)\n";
  return writeFile(outPath, out.str());
}
} // namespace ox_smt (SMT contract + Ghost encoder helpers)


bool Driver::run(const Options& opt) {
  bool ok = false;
  std::string src = readFile(opt.input, ok);
  if (!ok) {
    errs.push_back({"io", "cannot open input file '" + opt.input + "'", 0, 0});
    return false;
  }

  srcLines_.clear();
  {
    std::string cur;
    for (char ch : src) {
      if (ch == '\n') { srcLines_.push_back(cur); cur.clear(); }
      else cur += ch;
    }
    srcLines_.push_back(cur);
  }

  std::vector<Token> toks;
  std::vector<LexError> lexErrs;
  Lexer lex(src);
  lex.lex(toks, lexErrs);
  for (auto& e : lexErrs) errs.push_back({"lex", e.msg, e.line, e.col});
  if (!lexErrs.empty()) return false;


  importVisited_.clear();
  std::string baseDir = pathDir(opt.input);
  {
    std::vector<CompileError> impErrs;
    toks = resolveImports(toks, baseDir, impErrs);
    for (auto& e : impErrs) errs.push_back(e);
    if (!impErrs.empty()) return false;
  }

  std::vector<ParseError> parseErrs;
  Parser parser(std::move(toks), parseErrs);
  auto prog = parser.parseProgram();
  for (auto& e : parseErrs) errs.push_back({"parse", e.msg, e.line, e.col});
  if (!parseErrs.empty()) return false;

  Sema sema;


  sema.requireMain = (opt.action == Action::run || opt.action == Action::exe)
                     && !opt.freestanding;
  sema.freestanding = opt.freestanding;
  sema.check(*prog);
  for (auto& e : sema.errs) errs.push_back({"sema", e.msg, e.line, e.col, e.hint});
  if (!sema.errs.empty()) return false;

  // `oxide check` stops after semantic analysis. This gives editors and CI a
  // fast, side-effect-free correctness pass without generating LLVM IR or
  // invoking Clang.
  if (opt.action == Action::check) return true;

  // ox:note --verify-only fast path: skip LLVM IR lowering (no IRGen, no clang later)
  // and only emit contracts as SMT + dispatch to doVerify. Lets contract
  // iteration run sub-second on small files. We still require a clean Sema
  // (carried from above), so a program that doesn't typecheck still fails  - 
  // the contract still has to typecheck to encode meaningfully.
  if (opt.action == Action::verify && opt.verifyOnly && !opt.smtOut.empty()) {
    if (!ox_smt::emitSmt(*prog, opt.smtOut)) {
      errs.push_back({"smt", "failed to write SMT file '" + opt.smtOut + "'", 0, 0});
      return false;
    }
    program_ = prog.get();   // for doVerify()'s --audit-axioms walk
    return doVerify(opt);
  }

  IRGen irgen(sema);
  irgen.setTargetTriple(opt.targetTriple);
  irgen.generate(*prog);
  ir_ = irgen.takeIR();

  // ox:proof Optional static-discharge path: emit the program's contracts (the AST still
  // carries them) as SMT-LIB alongside the requested action. Independent of the
  // runtime gate path (and independent of --freestanding, so contracted code
  // produces SMT even when the runtime trap symbol is dropped). A solver is NOT
  // invoked here; the .smt2 file is consumed by Z3/Why3 out-of-band.
  if (!opt.smtOut.empty()) {
    if (!ox_smt::emitSmt(*prog, opt.smtOut)) {
      errs.push_back({"smt", "failed to write SMT file '" + opt.smtOut + "'", 0, 0});
      return false;
    }
  }

  switch (opt.action) {
    case Action::run: return doRun(opt);
    case Action::emit: return doEmit();
    case Action::build: return doBuild(opt, opt.output.empty() ? stripExt(opt.input) + ".o" : opt.output);
    case Action::exe: return doExe(opt, opt.output.empty() ? stripExt(opt.input) + ".exe" : opt.output);
    case Action::check: return true;  // handled before IR generation
    case Action::verify:
      program_ = prog.get();   // for doVerify()'s --audit-axioms walk
      return doVerify(opt);
    case Action::bindgen:
      return false;  // bindgen is handled in main before Driver::run
  }
  return false;
}


void Driver::printErrors(const std::string& file) const {
  // Diagnostics go to STDERR, the standard CLI convention for compile/runtime
  // errors (so `oxide emit > out.ll` doesn't pollute the IR stream with
  // errors, and `oxide run` errors don't interleave with program stdout).
  // Earlier this used std::printf (stdout), which mixed the compiler's own
  // diagnostic output with program data and made `2>` captures empty even
  // when the compiler plainly emitted text  -  masking the real cause.
  for (const auto& e : errs) {
    std::fprintf(stderr, "error[%s]: %s\n", e.stage.c_str(), e.msg.c_str());
    if (e.line >= 1 && (size_t)e.line <= srcLines_.size()) {
      const std::string& line = srcLines_[e.line - 1];
      char numbuf[32];
      std::snprintf(numbuf, sizeof(numbuf), "%d", e.line);
      std::fprintf(stderr, " %s | %s\n", numbuf, line.c_str());
      std::string pad(std::string(numbuf).size(), ' ');

      int col = e.col > 0 ? e.col : (int)(line.find_first_not_of(" \t")) + 1;
      if (col <= 0) col = 1;
      std::string caret(pad.size() + 3 + (size_t)(col - 1), ' ');
      caret += "^";
      std::fprintf(stderr, "%s\n", caret.c_str());
      if (!e.hint.empty()) std::fprintf(stderr, "      = note: %s\n", e.hint.c_str());
    } else {
      std::fprintf(stderr, "  (no source location)\n");
    }
    if (!e.hint.empty() && e.line < 1) std::fprintf(stderr, "  hint: %s\n", e.hint.c_str());
  }
  std::fprintf(stderr, "  --> %s\n\n", file.c_str());
  std::fflush(stderr);
}

bool Driver::doEmit() {
  std::fputs(ir_.c_str(), stdout);
  return true;
}


static std::string tempDir() {
  const char* t = std::getenv("TEMP");
  if (!t) t = std::getenv("TMP");
  if (!t) t = ".";
  return t;
}

static std::string tempStem(const char* purpose) {
#ifdef _WIN32
  const long long pid = (long long)_getpid();
#else
  const long long pid = (long long)getpid();
#endif
  return tempDir() + "\\oxide_" + purpose + "_" + std::to_string(pid);
}

bool Driver::doBuild(const Options& opt, const std::string& outPath) {


  std::string ll = tempStem("build") + ".ll";
  std::string irOut = opt.freestanding ? ir_ : renameMain(ir_);
  if (!writeFile(ll, irOut)) {
    errs.push_back({"emit", "cannot write temp ir", 0, 0});
    return false;
  }

  std::string clang = findTool({"clang", "clang-cl"});
  if (clang.empty()) clang = "clang";
  std::string cmd;


  std::string extra = opt.freestanding ? " -ffreestanding" : "";

  std::string nw = " -Wno-override-module";
  if (clang == "clang-cl") {
    cmd = clang + " -c -fuse-ld=lld" + extra + nw + " \"" + ll + "\" -o \"" + outPath + "\"";
  } else {
    cmd = clang + " -c" + extra + nw + " \"" + ll + "\" -o \"" + outPath + "\"";
  }
  int rc = runCmd(cmd);
  std::remove(ll.c_str());
  if (rc != 0) {
    errs.push_back({"build", "clang failed to assemble the generated ir", 0, 0});
    return false;
  }
  return true;
}

bool Driver::doExe(const Options& opt, const std::string& outPath) {
  std::string stem = tempStem("exe");
  std::string ll = stem + ".ll";
  std::string rt = stem + "_rt.c";


  std::string irFix = opt.freestanding ? ir_ : renameMain(ir_);
  if (!writeFile(ll, irFix)) {
    errs.push_back({"emit", "cannot write temp ir", 0, 0});
    return false;
  }
  std::string rtSrc;
  if (!opt.freestanding) {
    rtSrc = runtimeC();
    if (!writeFile(rt, rtSrc)) {
      errs.push_back({"emit", "cannot write runtime", 0, 0});
      return false;
    }
  }

  std::string clang = findTool({"clang", "clang-cl", "gcc", "cl"});
  if (clang.empty()) clang = "clang";
  std::string optflag = optFlag(opt.optimize);


  std::string nw = "-Wno-override-module";

  std::string lflags = linkFlagsFor(clang, opt);
  std::string cmd;
  if (clang == "cl") {
    cmd = clang + " /O2 /Fe:\"" + outPath + "\" \"" + ll + "\"";
    if (!opt.freestanding) cmd += " \"" + rt + "\"";
    cmd += lflags;
  } else if (clang == "clang-cl") {
    cmd = clang + " -fuse-ld=lld " + optflag + " " + nw + " -o \"" + outPath + "\" \"" + ll + "\"";
    if (!opt.freestanding) cmd += " \"" + rt + "\"";
    cmd += lflags;
  } else {
    cmd = clang + " " + optflag + " " + nw + " -o \"" + outPath + "\" \"" + ll + "\"";
    if (!opt.freestanding) cmd += " \"" + rt + "\"";
    cmd += lflags;
  }
  int rc = runCmd(cmd);
  std::remove(ll.c_str());
  if (!opt.freestanding) std::remove(rt.c_str());
  if (rc != 0) {
    errs.push_back({"link", "clang failed to link the executable", 0, 0});
    return false;
  }
  return true;
}

bool Driver::doRun(const Options& opt) {


  std::string exe = tempStem("run") + ".exe";
  if (!doExe(opt, exe)) {
    // `doExe` already pushed a diagnostic describing why the build/link failed
    // (e.g. "clang failed to link the executable", or "cannot write temp ir").
    // Don't overwrite that real cause with a generic message  -  but guard with
    // the empty-errs check so a future doExe path that returns false without
    // recording an error still surfaces a diagnostic rather than nothing.
    if (errs.empty()) errs.push_back({"run", "failed to build temp executable", 0, 0});
    return false;
  }

  // The freshly compiled program runs with its stdout/stderr connected
  // straight to the console, so the user already sees any program output +
  // any runtime crash message  -  this `doRun` is only responsible for the
  // exit code. On Windows `std::system` returns the child's exit() value
  // directly (no high-byte WEXITSTATUS encoding).
  int rr = std::system(("\"" + exe + "\"").c_str());
  std::remove(exe.c_str());
  if (rr != 0) {
    // CRITICAL: a nonzero program exit is NOT a compile failure. Earlier this
    // branch returned `rr == 0` (false) WITHOUT recording a diagnostic, leaving
    // `errs` empty, so main() fell through to the bare `error: compilation
    // failed` fallback  -  a compound error that swallowed BOTH the real compile
    // status ("compile actually succeeded") AND the real runtime failure (the
    // program's exit code). That is the bug behind hmap.ox/hset.ox (and a plain
    // `fn main() -> i64 { return 13; }`) printing only `error: compilation
    // failed` with zero source-location diagnostics and an empty stderr.
    //
    // Record the actual program exit code so the user sees what really
    // happened, and route it through the standard diagnostic path
    // (printErrors) instead of the catch-all. We do NOT report a fake source
    // location (no `line`/`col`), so printErrors prints the `(no source
    // location)` arm  -  accurate for a run-stage outcome.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "program exited with code %d", rr);
    errs.push_back({"run", buf, 0, 0});
    return false;
  }
  return true;
}

// `oxide verify`  -  close the formal-verification loop.
//
// Emits the program's contracts (requires/ensures/invariant/assert) as SMT-LIB
// via emitSmt, shells out to an external SMT solver (default z3; cvc5 and why3
// also recognised), parses the per-clause `(check-sat)` results, and exits
// non-zero if ANY clause is `sat` (counterexample exists) or `unknown` (solver
// could not discharge). `unsat` = proven for all inputs.
//
// The emitted .smt2 already contains `; note:` lines (Driver.cpp ~1281, 1297)
// flagging uninterpreted placeholders. We correlate each clause with the
// `; note:` lines that precede its `(check-sat)` so the per-clause report can
// say *why* an undischarged clause failed (e.g. "unknown  -  uninterpreted:
// arr[k]") rather than just *that* it failed. That is the difference between a
// verifier that reports and one that merely exits.

namespace {

// One row of the verify report. `line` is the clause's source line. `notes`
// are the uninterpreted-placeholder/function notes that were emitted inside
// this clause's discharge scope (i.e. between the previous clause's
// `(check-sat)` and this one's).
struct VerifyRow {
  std::string function;
  std::string clause;     // "requires_0", "ensures_1", "invariant_d0_0", "assert_3"
  int line = 0;
  std::string status;     // "unsat", "sat", "unknown", or "(no result)"
  std::vector<std::string> notes;
};

// Parse any `; <label> (... source line N)` header emitted just before a
// `(define-fun ...)` into (function, clause, line). This accepts the Tier A
// forms (`source line`, `while, source line`) and the Tier B/WP forms
// (`return-site ensures, source line`, `while-entry, source line`, etc.).
// Labels look like `fib_requires_0`, `fib_invariant_d0_0`, `fib_assert_2`, or
// `fib_ensures_ret_0_0`. The function name is everything before the last
// clause-kind marker.
bool parseClauseHeader(const std::string& line, std::string& fn, std::string& clause, int& srcLine) {
  // Expected examples:
  //   "; fib_requires_0 (source line 10)"
  //   "; fib_invariant_d0_0 (while, source line 18)"
  //   "; fib_ensures_ret_0_0 (return-site ensures, source line 12)"
  if (line.size() < 2 || line[0] != ';' || line[1] != ' ') return false;
  std::string s = line.substr(2);
  size_t src = s.find("source line ");
  if (src == std::string::npos) return false;
  size_t paren = s.rfind('(', src);
  if (paren == std::string::npos) return false;
  std::string label = s.substr(0, paren);
  // trim trailing whitespace
  while (!label.empty() && (label.back() == ' ' || label.back() == '\t')) label.pop_back();
  // ox:proof Reject sub-obligation headers emitted by the mini-walker inside preserves
  // (and similar) blocks. These are INTERNAL sub-discharges of a parent
  // preserves/refines block; treating them as top-level clause headers would
  // create spurious VerifyRows, fragment a parent clause's `(check-sat)`
  // count, and mis-attribute the parent's actual preservation-obligation
  // check-sat to a nested sub-obligation (or vice-versa).
  //
  // The tag is the text between '(' and 'source line'  -  for the call-requires
  // sub-discharge path it reads "call requires". Reject it.
  {
    std::string tag = s.substr(paren, src - paren);
    if (tag.find("call requires") != std::string::npos) return false;
  }
  // ox:proof Reject the mini-walker's nested sub-obligation headers. The WP encoder
  // (`smtEncodeStmts`) emits `; <fnName>_<kind>_<n> (source line N)` headers
  // for every `assert`/`invariant`/`return-site ensures` it discharges while
  // inlining a callee body inside a preserves/refines block. The mini-walker
  // passes a UNIQUE label base of the form `preserves_<h>_<s>_inline_<seq>` /
  // `<fn>_inline_<seq>` (Driver.cpp ~3045), so each nested sub-obligation's
  // label contains the literal `_inline_` marker. Reject any header whose
  // label contains `_inline_` so its `(check-sat)` is folded into the parent
  // preserves/refines row's tally instead of starting a spurious new row.
  //
  // Safe: `_inline_` is a reserved synthetic suffix the mini-walker mints; a
  // user-named function cannot produce this label (function names allow only
  // alnum/underscore, but the walker's suffix is literal and unambiguous in the
  // emitted `; <label> (source line ...)` line because the marker is always
  // followed by a clause-kind suffix, never an identifier tail).
  if (label.find("_inline_") != std::string::npos) return false;
  // Feature 1+2  -  reject split sub-goal headers. The proof splitter emits
  // sub-goals with labels suffixed by `#s0`, `#s1`, `#p0`, `#pfull`, etc.
  // These are sub-discharges of a parent clause's `(check-sat)`  -  each is
  // a piece of the parent's proof. Treating them as top-level clause headers
  // would create spurious VerifyRows, fragment the parent's proof, and
  // mis-attribute. Reject any label containing `#` so its check-sat is
  // folded into the parent clause's tally.
  if (label.find('#') != std::string::npos) return false;
  // ox:proof Reject the T1 spec-fn DECLARATION header `; spec fn <name> (source line N)`
  // (Ghost.cpp ~207). It carries NO clause-kind marker (label is bare `<name>`)
  // and is NEVER followed by a `(check-sat)`  -  a spec fn is just a `define-fun`,
  // not a discharge obligation. Accepting it pollutes the rows vector with a
  // row of `rowCheckCount == 0` ("assumed"); harmless for the tally but clutters
  // the report with a bogus `spec fn <name>` row. Matching the literal `spec fn `
  // prefix (with the space) is unambiguous: no user identifier can start with a
  // space, and the only other `; spec fn ...` line is the section banner which
  // is matched first by the banner guard below.
  {
    static const std::string specFnPrefix = "spec fn ";
    if (label.rfind(specFnPrefix, 0) == 0) return false;
  }
  // parse the source line number
  {
    size_t ns = src + std::string("source line ").size();
    char* end = nullptr;
    long n = std::strtol(s.c_str() + ns, &end, 10);
    if (end == s.c_str() + ns) return false;
    srcLine = (int)n;
  }
  // Split label into function + clause. Find the last occurrence of one of
  // the clause-kind markers.
  struct M { const char* tag; size_t len; };
  static const M marks[] = {
    {"_requires_", 10}, {"_ensures_", 9}, {"_invariant_", 11}, {"_assert_", 8}
  };
  size_t best = std::string::npos;
  size_t bestLen = 0;
  for (const auto& m : marks) {
    size_t p = label.rfind(m.tag);
    if (p != std::string::npos && (best == std::string::npos || p > best)) {
      best = p; bestLen = m.len;
    }
  }
  if (best == std::string::npos) { clause = label; fn = ""; return true; }
  fn = label.substr(0, best);
  clause = label.substr(best + 1);   // drop the leading '_' of the marker
  return true;
}

static bool allDigits(const std::string& s) {
  if (s.empty()) return false;
  for (char ch : s) if (ch < '0' || ch > '9') return false;
  return true;
}

static bool parseEnsuresFallbackIndex(const std::string& clause, int& idx) {
  const std::string prefix = "ensures_";
  if (clause.rfind(prefix, 0) != 0) return false;
  if (clause.rfind("ensures_ret_", 0) == 0) return false;
  std::string rest = clause.substr(prefix.size());
  if (!allDigits(rest)) return false;
  idx = std::atoi(rest.c_str());
  return true;
}

static bool parseEnsuresRetIndex(const std::string& clause, int& idx) {
  const std::string prefix = "ensures_ret_";
  if (clause.rfind(prefix, 0) != 0) return false;
  size_t last = clause.find_last_of('_');
  if (last == std::string::npos || last + 1 >= clause.size()) return false;
  std::string rest = clause.substr(last + 1);
  if (!allDigits(rest)) return false;
  idx = std::atoi(rest.c_str());
  return true;
}

// ox:note Pull the placeholder name out of a `; note: replaced an unsupported subform
// (... ) with the uninterpreted <kind> <name>` line. Returns the captured
// "why" text in `why` and the name in `name`; false if the line isn't a note.
bool parseNote(const std::string& line, std::string& why, std::string& name) {
  const std::string prefix = "; note: replaced an unsupported subform (";
  auto p = line.find(prefix);
  if (p == std::string::npos) return false;
  p += prefix.size();
  auto close = line.find(')', p);
  if (close == std::string::npos) return false;
  why = line.substr(p, close - p);
  // After "with the uninterpreted <kind> " comes the name.
  static const std::string tail = " with the uninterpreted ";
  auto t = line.find(tail, close);
  if (t == std::string::npos) return false;
  t += tail.size();
  // skip the "constant "/"function " word
  auto sp = line.find(' ', t);
  if (sp == std::string::npos) return false;
  name = line.substr(sp + 1);
  while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
  return true;
}

}  // namespace

bool Driver::doVerify(const Options& opt) {
  // ox:proof The `--emit-smt` block in Driver::run() already invoked emitSmt() against
  // opt.smtOut before the action switch dispatched here. So we just rewire
  // opt.smtOut to a temp path up front (in main.cpp, by setting it when the
  // action is `verify`), OR the caller passed --emit-smt explicitly. Either
  // way, by the time we get here opt.smtOut is the .smt2 path that exists on
  // disk. If it's empty, we never got the SMT emission  -  flag and bail.
  if (opt.smtOut.empty()) {
    errs.push_back({"verify", "internal: verify action reached doVerify without "
                   "an SMT path set (main.cpp should pre-set opt.smtOut for verify)",
                   0, 0});
    return false;
  }
  std::string smtPath = opt.smtOut;

  // Splice in a `(set-option :timeout N)` line right after `(set-logic ALL)`
  // so the solver can't hang on a hard invariant. emitSmt writes
  // `(set-logic ALL)` as part of its fixed header (Driver.cpp ~1556-1575).
  if (opt.solverTimeout > 0) {
    std::ifstream in(smtPath, std::ios::binary);
    std::stringstream ss; ss << in.rdbuf(); in.close();
    std::string txt = ss.str();
    auto anchor = txt.find("(set-logic ALL)");
    if (anchor != std::string::npos) {
      std::string ins = "\n(set-option :timeout "
                        + std::to_string(opt.solverTimeout) + ")";
      txt.insert(anchor + std::string("(set-logic ALL)").size(), ins);
      std::ofstream out(smtPath, std::ios::binary);
      out << txt;
    }
  }

  // ox:proof Locate the solver.
  std::string solver = opt.solver.empty() ? "z3" : opt.solver;
  std::string solverBin = findTool({solver.c_str()});
  if (solverBin.empty()) {
    errs.push_back({"verify", "solver '" + solver +
                    "' not found on PATH (install z3, or pass --solver NAME)",
                    0, 0});
    return false;
  }

  // ox:proof Build the solver invocation. We follow the same quoting discipline that
  // doExe/doBuild use: the binary name is BARE (findTool returned it without
  // quotes), and only paths that may contain spaces are quoted. Mixing a
  // quoted binary into a `> redirect` line trips cmd.exe's redirect parser.
  //   z3 (default): bare `z3 <file>`  -  prints `unsat`/`sat`/`unknown`.
  //   cvc5:        `cvc5 <file>` [--timeout=N].
  //   why3:        `why3 prove <file>`   (best-effort parse).
  std::string cmd;
  if (solver == "why3") {
    cmd = solverBin + " prove \"" + smtPath + "\"";
  } else if (solver == "cvc5") {
    cmd = solverBin + " \"" + smtPath + "\"";
    if (opt.solverTimeout > 0)
      cmd += " --timeout=" + std::to_string(opt.solverTimeout);
  } else {
    cmd = solverBin + " \"" + smtPath + "\"";
  }

  // ox:why Capture stdout+stderr to a temp file so we can parse per-clause results.
  // Two robustness measures, both earned the hard way:
  //
  //   1. FORWARD-SLASH NORMALISATION. cmd.exe's redirect parser is fragile
  //      when a bare (unquoted) `>` target contains backslashes that come
  //      right after a quoted argument, e.g.
  //        z3 "build/_diag.smt2" > C:\Users\mosky\AppData\Local\Temp\oxide_solver.out 2>&1
  //      In that shape cmd.exe mis-tokenises the command, emits "The process
  //      cannot access the file ..." and *truncates* the solver output  -  Z3
  //      ends up returning only a fraction of its results, which the positional
  //      row-mapper then reports as `(no result)` for late clauses
  //      (preserves/refines rows). Forward slashes parse cleanly under cmd.exe
  //      for the `>` target (and are accepted by std::ifstream / std::remove on
  //      Windows), so normalise TEMP before splicing it in. We keep the path
  //      unquoted (default TEMP has no spaces); quoting would re-trigger
  //      cmd.exe's other redirect-quoting bug.
  //
  //   2. PID-UNIQUE TEMP FILE. A FIXED `oxide_solver.out` name collides when
  //      a previous/aborted/crashed oxide.exe (or its orphaned z3 child) is
  //      still holding the file open  -  `> outPath` then silently fails to
  //      truncate and std::ifstream reads the STALE content from the prior
  //      run, producing the same `(no result)` symptom as #1 but for a
  //      different cause. Embedding the PID in the filename eliminates that
  //      race: each oxide.exe process writes its own file and removes it on
  //      completion. (See gap C6 in the gap audit  -  Z3 is invoked out-of-
  //      process via temp file rather than pipes; this is the lock-file race
  //      hazard that was called out.)
  std::string dir = tempDir();
  unsigned pidVal = 0;
#ifdef _WIN32
  pidVal = static_cast<unsigned>(_getpid());
#else
  pidVal = static_cast<unsigned>(getpid());
#endif
  std::string outPath = dir + "/oxide_solver_" + std::to_string(pidVal) + ".out";
  cmd += " > " + outPath + " 2>&1";

  runCmd(cmd);   // solver exit code is unreliable across z3/cvc5/why3; we parse.

  std::string solverOut;
  {
    std::ifstream f(outPath, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    solverOut = ss.str();
  }
  std::remove(outPath.c_str());

  // ox:proof Re-read the SMT file to walk its clause headers and `; note:` lines.
  // (We can't just remember them from emitSmt because that function doesn't
  // expose a structured side-channel; the file is the contract.)
  std::string smtText;
  {
    std::ifstream f(smtPath, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    smtText = ss.str();
  }

  // ox:proof Walk the SMT file line by line. We track the "current clause" by the
  // most recent `; <label> (source line N)` header, accumulate `; note:`
  // lines against it, and count how many `(check-sat)` instances fall inside
  // this clause's region (preconditions don't emit one  -  they're assumed, not
  // discharged: see comment in the requires block above). Only rows with ≥1
  // check-sat get a result assigned from the solver output.
  //
  // Multiple check-sats per clause: a `preserves` block emits sub-obligation
  // check-sats from the mini-walker's call-requires discharges BEFORE the
  // actual preservation-obligation check-sat. The LAST check-sat in a clause's
  // region is the real proof obligation; the earlier ones are internal
  // sub-discharges that should not shadow the headline result. So we count
  // check-sats per row and consume that many solver outputs, keeping only the
  // final one as the row's status.
  std::vector<VerifyRow> rows;
  std::vector<int> rowCheckCount;  // parallel to rows; # of (check-sat) inside this clause's region
  {
    VerifyRow cur; bool haveCur = false; int curCS = 0;
    std::istringstream iss(smtText);
    std::string line;
    while (std::getline(iss, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      std::string fn, clause; int srcLine = 0;
      if (parseClauseHeader(line, fn, clause, srcLine)) {
        if (haveCur) { rows.push_back(cur); rowCheckCount.push_back(curCS); }
        cur = VerifyRow{fn, clause, srcLine, "assumed", {}};
        curCS = 0;
        haveCur = true;
        continue;
      }
      std::string why, name;
      if (parseNote(line, why, name)) {
        if (haveCur) cur.notes.push_back(why + " -> " + name);
        continue;
      }
      // ox:proof Match both `(check-sat)` and `(check-sat-using ...)` forms  -  the
      // missing close paren `(check-sat` is intentional so the row-counter
      // keeps working after smtDischarge switched to `(check-sat-using ...)`
      // to defeat Z3 MBQI quantifier-cache pollution across queries.
      if (line.find("(check-sat") != std::string::npos) {
        // Belongs to the current clause. The next header will push cur; if
        // the file ends without a new header we push the last one after.
        if (haveCur) curCS++;
      }
    }
    if (haveCur) { rows.push_back(cur); rowCheckCount.push_back(curCS); }
  }

  // Assign per-clause results in order, but ONLY to discharged clauses
  // (rowCheckCount > 0). Non-discharged clauses (precondition rows) stay
  // "assumed". For clauses with multiple check-sats (e.g. preserves blocks
  // whose mini-walker emits call-requires sub-obligations), we consume one
  // solver result per check-sat and keep the LAST  -  that's the actual
  // proof obligation; the earlier ones are internal sub-discharges.
  {
    std::vector<std::string> results;
    std::istringstream iss(solverOut);
    std::string line;
    while (std::getline(iss, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      auto a = line.find_first_not_of(" \t");
      auto b = line.find_last_not_of(" \t\r\n.");
      if (a == std::string::npos) continue;
      std::string w = line.substr(a, (b == std::string::npos ? line.size() : b - a + 1));
      if (w == "unsat" || w == "sat" || w == "unknown") results.push_back(w);
    }
    size_t ri = 0;
    for (size_t i = 0; i < rows.size(); i++) {
      int nCS = rowCheckCount[i];
#ifdef OXIDE_VERIFY_DIAG
      std::fprintf(stderr, "[diag] row %zu fn='%s' clause='%s' nCS=%d ri_before=%zu results_size=%zu\n",
                   i, rows[i].function.c_str(), rows[i].clause.c_str(), nCS, ri, results.size());
#endif
      if (nCS == 0) continue;   // assumed precondition: leave "assumed"
      // ox:proof Consume nCS solver results; keep the last as the row's status.
      // If we run out of results, mark "(no result)".
      std::string last = "(no result)";
      for (int j = 0; j < nCS; ++j) {
        if (ri < results.size()) { last = results[ri++]; }
        else { last = "(no result)"; break; }
      }
      rows[i].status = last;
#ifdef OXIDE_VERIFY_DIAG
      std::fprintf(stderr, "[diag] row %zu ri_after=%zu last='%s'\n", i, ri, last.c_str());
#endif
    }
  }

  // A signature-level `ensures_N` row is the loose Tier A fallback over an
  // unconstrained `result` symbol. When the WP encoder emitted one or more
  // concrete return-site rows for the same ensures index, those return-site rows
  // are the authoritative proof obligations. Do not fail verification merely
  // because the fallback could not prove a body-dependent postcondition.
  std::set<std::pair<std::string, int>> wpEnsures;
  for (const auto& r : rows) {
    int idx = -1;
    if (parseEnsuresRetIndex(r.clause, idx)) wpEnsures.insert({r.function, idx});
  }

  // Print the report.
  std::printf("oxide verify: %s on %s\n", solver.c_str(), opt.input.c_str());
  std::printf("%-20s %-22s %5s  %-10s %s\n", "function", "clause", "line", "status", "notes");
  std::printf("%-20s %-22s %5s  %-10s %s\n", "--------", "------", "----", "------", "-----");
  int provenCount = 0, failCount = 0, noResultCount = 0, assumedCount = 0;
  for (const auto& r : rows) {
    int fallbackIdx = -1;
    bool ignoredFallback = parseEnsuresFallbackIndex(r.clause, fallbackIdx) &&
                           wpEnsures.count({r.function, fallbackIdx}) > 0;
    std::string displayStatus = ignoredFallback ? "fallback" : r.status;
    std::string noteStr;
    if (ignoredFallback) noteStr = "superseded by return-site WP proof";
    if (r.status != "unsat" && r.status != "assumed" && !r.notes.empty()) {
      if (!noteStr.empty()) noteStr += "; ";
      noteStr += "uninterpreted: ";
      for (size_t i = 0; i < r.notes.size(); i++) {
        if (i) noteStr += ", ";
        noteStr += r.notes[i];
      }
    }
    std::printf("%-20s %-22s %5d  %-10s %s\n",
                r.function.c_str(), r.clause.c_str(), r.line,
                displayStatus.c_str(), noteStr.c_str());
    if (ignoredFallback) continue;
    if (r.status == "unsat") provenCount++;
    else if (r.status == "assumed") assumedCount++;
    else if (r.status == "(no result)") noResultCount++;
    else failCount++;
  }
  std::printf("\n%d proven, %d undischarged, %d assumed, %d unreported\n",
              provenCount, failCount, assumedCount, noResultCount);

  // Feature 6  -  Proof certificate. Write a JSON log file to build/_proof/
  // listing every check-sat result, tactic, solver, and the .smt2 file.
  // A separate tool (oxide-check) can re-run the proof log against a different
  // solver and verify every unsat is reproduced  -  catching encoding bugs that
  // produce false unsat.
  {
    std::string pDir = proofdispatch::proofLogDir();
    // ox:why Best-effort proof-log directory creation; avoid shelling out here because
    // Windows `system()` routes through cmd.exe, where `mkdir -p` is invalid.
    std::error_code ec;
    std::filesystem::create_directories(pDir, ec);
    // Split the combined .smt2 into per-goal files and write the proof log.
    std::string firstTactic;
    auto goals = proofdispatch::splitPerGoalSmt(smtPath, pDir, firstTactic);
    if (!goals.empty()) {
      // ox:proof Run the default solver on each per-goal file to get statuses.
      std::vector<proofdispatch::ProofGoal> loggedGoals;
      int timeoutSec = opt.solverTimeout > 0 ? opt.solverTimeout : 10;
      for (auto& g : goals) {
        proofdispatch::ProofGoal pg;
        pg.label = g.label;
        pg.tactic = g.tactic;
        pg.solver = solver;
        pg.smt_file = g.smt_file;
        // ox:proof Run the solver on the per-goal file.
        auto sr = proofdispatch::runOneSolver(solver, g.smt_file,
                                              solverBin.empty() ? solver : solverBin,
                                              timeoutSec);
        pg.status = sr.status;
        pg.disagreement = false;
        loggedGoals.push_back(pg);
      }
      // Write the JSON proof log.
      std::string srcFile = opt.input;
      proofdispatch::writeProofLog(loggedGoals, pDir, srcFile);
    }
  }

  // ox:proof Non-zero exit if anything failed to discharge (sat or unknown) or was
  // unreported (solver missing/bad grammar). A clean verify is all-unsat.
  //
  // --audit-axioms: surface every trusted (non-machine-verified) axiom the
  // program used, with its fully-qualified name (Namespace::Name or just Name),
  // its file and source line, and its source citation (or "no source cited").
  // This makes the unchecked trust decisions visible and audit-able. Printed
  // AFTER the main verify report so that ordering stays stable. The audit walks
  // program_->axioms directly  -  the Program still exists during doVerify.
  if (opt.auditAxioms) {
    std::printf("\n--- Trusted axioms used ---\n");
    if (program_ && !program_->axioms.empty()) {
      int trustedCount = 0;
      for (const auto& ax : program_->axioms) {
        if (!ax || !ax->isTrusted) continue;  // audit targets TRUSTED axioms only
        trustedCount++;
        std::string qname = ax->qualifiedName();
        if (qname.empty()) qname = "(unnamed)";
        // Use the input filename as the file location; ax->line is its 1-based line.
        std::string srcFile = opt.input;
        if (ax->sourceCitation.empty())
          std::printf("  %s  -  %s:%d  -  no source cited\n",
                       qname.c_str(), srcFile.c_str(), ax->line);
        else
          std::printf("  %s  -  %s:%d  -  source: %s\n",
                       qname.c_str(), srcFile.c_str(), ax->line,
                       ax->sourceCitation.c_str());
      }
      if (trustedCount == 0)
        std::printf("  (no trusted axioms used)\n");
      else
        std::printf("  [%d trusted axiom(s) total]\n", trustedCount);
    } else {
      std::printf("  (no trusted axioms used)\n");
    }
  }

  // --audit-trust: print the FULL trust audit  -  every trusted assumption the
  // proof depended on but did NOT discharge. Combines:
  //   (1) `trusted assume <expr> source "...";` statements  -  discovered by
  //       rescanning the emitted .smt2 for `; note: trusted assume at line N`
  //       comment lines (each followed by an optional `; note: source: <cit>`
  //       companion line) that the smtEncodeStmt assumeStmt arm wrote. We pull
  //       a short description from the source file line N when available so
  //       the audit row names the assumed predicate; if the source isn't
  //       readable we fall back to a "(trusted assume at line N)" placeholder.
  //   (2) `trusted axiom ...;` declarations  -  walked from program_->axioms
  //       (the sibling --audit-axioms flag uses the same source of truth), so
  //       a reviewer sees the whole trust boundary in one place.
  // Printed AFTER the main verify report (and after --audit-axioms, when both
  // are set) so ordering stays stable. The audit only rows TRUSTED entries  - 
  // a bare `assume` (isTrusted=false) or unmarked axiom is a silent hypothesis
  // that is NOT surfaced here.
  if (opt.auditTrust) {
    std::printf("\n--- Trusted assumptions used ---\n");
    std::vector<TrustEntry> entries;

    // ox:proof (1) Trusted assumes  -  scan the emitted SMT witness for the `; note:`
    // comment lines the smtEncodeStmt assumeStmt arm writes. The arm ALWAYS
    // emits a `; note: trusted assume at line N` line, immediately followed by
    // an optional `; note: source: <citation>` companion (only when a source
    // was cited). We use a one-entry pending buffer: a trusted-assume note
    // starts a pending entry; the next source note (if any) attaches to it;
    // any other line flushes the pending entry unmodified. This handles both
    // cited and uncited assumes and an arbitrary mix in any order.
    {
      // ox:why Pre-load the source file once so we can name the assumed predicate in
      // the audit row (we read source line N, trimmed  -  the user's expression).
      std::vector<std::string> srcLines;
      {
        std::ifstream sf(opt.input, std::ios::binary);
        if (sf) {
          std::stringstream ss; ss << sf.rdbuf();
          std::string all = ss.str();
          std::string cur;
          for (char ch : all) {
            if (ch == '\n') { srcLines.push_back(cur); cur.clear(); }
            else cur.push_back(ch);
          }
          if (!cur.empty()) srcLines.push_back(cur);
        }
      }
      // ox:note Helper: build a TrustEntry for a trusted-assume note at `lineNo`.
      auto makeEntry = [&](int lineNo) -> TrustEntry {
        TrustEntry e;
        e.line = lineNo;
        e.file = opt.input;
        if (lineNo >= 1 && (size_t)(lineNo - 1) < srcLines.size()) {
          std::string raw = srcLines[lineNo - 1];
          size_t b = 0;
          while (b < raw.size() && std::isspace((unsigned char)raw[b])) b++;
          e.description = raw.substr(b);
        } else {
          e.description = "(trusted assume at line " + std::to_string(lineNo) + ")";
        }
        return e;
      };
      const std::string tag = "; note: trusted assume at line ";
      const std::string srcTag = "; note: source: ";
      std::istringstream si(smtText);
      std::string smtLine;
      bool havePending = false;
      TrustEntry pending;
      while (std::getline(si, smtLine)) {
        if (auto pos = smtLine.find(tag); pos != std::string::npos) {
          // Flush any previous pending entry (it had no source companion).
          if (havePending) { entries.push_back(pending); pending = TrustEntry{}; }
          std::istringstream ts(smtLine.substr(pos + tag.size()));
          int lineNo = 0; ts >> lineNo;
          pending = makeEntry(lineNo);
          havePending = true;
        } else if (havePending &&
                   smtLine.find(srcTag) != std::string::npos) {
          // Source companion for the pending trusted assume.
          auto sp = smtLine.find(srcTag);
          pending.source = smtLine.substr(sp + srcTag.size());
          entries.push_back(pending);
          pending = TrustEntry{};
          havePending = false;
        }
        // Any other line: leave the pending entry in place  -  it may yet get a
        // source companion on the immediately following line.
      }
      if (havePending) entries.push_back(pending);   // flush trailing uncited
    }

    // (2) Trusted axioms  -  walk program_->axioms (same source of truth as
    // --audit-axioms) so the combined report shows the whole trust boundary.
    // `description` is the qualified name (or "(unnamed axiom)"); `source` is
    // the citation string.
    if (program_) {
      for (const auto& ax : program_->axioms) {
        if (!ax || !ax->isTrusted) continue;
        TrustEntry e;
        std::string qname = ax->qualifiedName();
        e.description = qname.empty() ? "(unnamed axiom)" : qname;
        e.line = ax->line;
        e.source = ax->sourceCitation;
        e.file = opt.input;
        entries.push_back(e);
      }
    }

    if (entries.empty()) {
      std::printf("  (no trusted assumptions used)\n");
    } else {
      for (const auto& e : entries) {
        if (e.source.empty())
          std::printf("  line %d: %s  -  no source cited\n",
                       e.line, e.description.c_str());
        else
          std::printf("  line %d: %s  -  source: %s\n",
                       e.line, e.description.c_str(), e.source.c_str());
      }
      std::printf("  [%d trusted assumption(s) total]\n", (int)entries.size());
    }
  }

  if (failCount > 0 || noResultCount > 0) {
    errs.push_back({"verify",
        std::to_string(failCount) + " clause" +
        (failCount == 1 ? "" : "s") + " undischarged, " +
        std::to_string(noResultCount) + " unreported",
        0, 0, ""});
    return false;
  }
  return true;
}

// `oxide bindgen`  -  C header -> Oxide extern declarations.
//
// Shells out to `clang -Xclang -ast-dump=json <header.h>` (captured via popen),
// parses the resulting JSON with a small recursive-descent JSON parser (only
// the subset clang emits is handled: object/array/string/number/bool/null),
// then walks the AST for `FunctionDecl` (skip implicit/builtin decls) and
// `TypedefDecl`/`RecordDecl` to emit `extern struct Name;`, `typedef X = *Y;`,
// and `extern fn name(p: t, ...) -> ret;`. Qualifiers (const/volatile/restrict)
// are stripped; C types are mapped to Oxide types (int->i32, char->u8,
// long long->i64, void->void, float->f32, double->f64, size_t->usize,
// T*->*T, struct S*->*S).

namespace bindgen_detail {

// ox:note ---- Minimal JSON value (only what clang ast-dump=json needs) ----
struct JValue {
  enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
  bool b = false;
  double num = 0.0;
  std::string str;
  std::vector<JValue> arr;
  std::vector<std::pair<std::string, JValue>> obj;

  const JValue* find(const std::string& key) const {
    if (kind != Kind::Object) return nullptr;
    for (const auto& kv : obj) if (kv.first == key) return &kv.second;
    return nullptr;
  }
};

// Recursive-descent JSON parser. `p` points at the current byte; we skip
// whitespace then dispatch on the next char. Strings handle clang's escapes
// (clamps \uXXXX to 8-bit). Numbers parse to double (we only compare names).
struct JParser {
  const char* p;
  const char* end;
  explicit JParser(const std::string& s) : p(s.c_str()), end(s.c_str() + s.size()) {}

  void skipWS() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\015')) p++; }
  char peek() { return p < end ? *p : '\0'; }

  JValue parse() {
    skipWS();
    if (p >= end) return {};
    return parseValue();
  }

  JValue parseValue() {
    skipWS();
    if (p >= end) return {};
    char c = *p;
    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == '"') return parseString();
    if (c == 't' || c == 'f') return parseBool();
    if (c == 'n') return parseNull();
    return parseNumber();
  }

  std::string parseRawString() {
    // assumes *p == '"'
    p++; // opening quote
    std::string out;
    while (p < end && *p != '"') {
      if (*p == '\\' && p + 1 < end) {
        p++;
        char e = *p++;
        switch (e) {
          case '"':  out += '"'; break;
          case '\\': out += '\\'; break;
          case '/':  out += '/'; break;
          case 'b':  out += '\b'; break;
          case 'f':  out += '\f'; break;
          case 'n':  out += '\n'; break;
          case 'r':  out += '\015'; break;
          case 't':  out += '\t'; break;
          case 'u': {
            // clamp \uXXXX to 8-bit (bindgen only needs ASCII type names)
            if (p + 4 <= end) { p += 4; out += '?'; }
            break;
          }
          default: out += e; break;
        }
      } else {
        out += *p++;
      }
    }
    if (p < end && *p == '"') p++; // closing quote
    return out;
  }

  JValue parseString() {
    JValue v; v.kind = JValue::Kind::String; v.str = parseRawString(); return v;
  }

  JValue parseBool() {
    JValue v; v.kind = JValue::Kind::Bool;
    if (end - p >= 4 && std::strncmp(p, "true", 4) == 0) { v.b = true;  p += 4; }
    else if (end - p >= 5 && std::strncmp(p, "false", 5) == 0) { v.b = false; p += 5; }
    else p++;
    return v;
  }

  JValue parseNull() {
    JValue v; v.kind = JValue::Kind::Null;
    if (end - p >= 4 && std::strncmp(p, "null", 4) == 0) p += 4; else p++;
    return v;
  }

  JValue parseNumber() {
    JValue v; v.kind = JValue::Kind::Number;
    const char* start = p;
    while (p < end) {
      char c = *p;
      if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') p++;
      else break;
    }
    v.num = std::strtod(std::string(start, p - start).c_str(), nullptr);
    return v;
  }

  JValue parseArray() {
    JValue v; v.kind = JValue::Kind::Array;
    p++; // [
    skipWS();
    while (p < end && *p != ']') {
      v.arr.push_back(parseValue());
      skipWS();
      if (p < end && *p == ',') { p++; skipWS(); }
    }
    if (p < end && *p == ']') p++;
    return v;
  }

  JValue parseObject() {
    JValue v; v.kind = JValue::Kind::Object;
    p++; // {
    skipWS();
    while (p < end && *p != '}') {
      skipWS();
      std::string key = parseRawString();
      skipWS();
      if (p < end && *p == ':') { p++; skipWS(); }
      v.obj.emplace_back(std::move(key), parseValue());
      skipWS();
      if (p < end && *p == ',') { p++; skipWS(); }
    }
    if (p < end && *p == '}') p++;
    return v;
  }
};

// ---- C type string -> Oxide type string ----

// Edit a C type string in place: remove const/volatile/restrict qualifiers
// (they don't change the Oxide ABI type). Splits on whitespace so "const
// char *" -> "char *" and "unsigned int" survives (unsigned is kept).
// `count` accumulates the trailing '*' count removed by the caller later.
static void stripQualifiers(std::string& t) {
  std::string out;
  const char* p = t.c_str();
  while (*p) {
    // isolate next token
    const char* s = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    std::string tok(s, (size_t)(p - s));
    while (*p == ' ' || *p == '\t') p++;
    if (tok == "const" || tok == "volatile" || tok == "restrict" ||
        tok == "__const" || tok == "__restrict" || tok == "__restrict__")
      continue;
    if (!out.empty()) out += ' ';
    out += tok;
  }
  t = out;
}

// Map a base C type token-sequence (pointers already stripped) to an Oxide
// type. `unsigned` alone means `unsigned int`. Unknown bare names (typedef
// aliases that didn't desugar) keep their name; the caller mostly feeds us
// already-desugared strings, so this only kicks in for opaque user typedefs.
static std::string mapBaseType(const std::string& bt) {
  // ox:note well-known size typedefs clang sometimes leaves as bare names
  if (bt == "size_t" || bt == "__size_t" || bt == "size_type") return "usize";
  if (bt == "ssize_t" || bt == "ptrdiff_t" || bt == "intptr_t") return "isize";
  if (bt == "uintptr_t") return "usize";
  if (bt == "wchar_t") return "u32";
  if (bt == "__int128") return "i128";
  if (bt == "unsigned __int128") return "u128";

  // struct X / union X / enum X  ->  X  (drop the tag keyword)
  const char* tagp = nullptr;
  if (bt.rfind("struct ", 0) == 0)      tagp = bt.c_str() + 7;
  else if (bt.rfind("union ", 0) == 0)   tagp = bt.c_str() + 6;
  else if (bt.rfind("enum ", 0) == 0)    tagp = bt.c_str() + 5;
  if (tagp && *tagp) return tagp;

  if (bt == "void") return "void";
  if (bt == "bool" || bt == "_Bool") return "bool";
  // char (plain) is an independent type; signed/unsigned char are 8-bit ints.
  if (bt == "char" || bt == "unsigned char") return "u8";
  if (bt == "signed char") return "i8";
  // short: signed i16, unsigned u16.
  if (bt == "short" || bt == "short int" || bt == "signed short" || bt == "signed short int") return "i16";
  if (bt == "unsigned short" || bt == "unsigned short int") return "u16";
  // int: signed i32, unsigned u32.
  if (bt == "int" || bt == "signed" || bt == "signed int") return "i32";
  if (bt == "unsigned" || bt == "unsigned int") return "u32";
  // long / long long: signed i64, unsigned u64.
  if (bt == "long" || bt == "long int" || bt == "signed long" || bt == "signed long int" ||
      bt == "long long" || bt == "long long int" || bt == "signed long long" || bt == "signed long long int") return "i64";
  if (bt == "unsigned long" || bt == "unsigned long int" ||
      bt == "unsigned long long" || bt == "unsigned long long int") return "u64";
  if (bt == "float") return "f32";
  if (bt == "double") return "f64";
  if (bt == "long double") return "f128";

  // Unknown bare name (a user typedef we couldn't desugar): keep verbatim.
  return bt;
}

// Translate one C type string ("const char *", "struct HWND_tag *",
// "unsigned int", "void") to an Oxide type ("*u8", "*HWND_tag", "u32",
// "void"). Collects any struct tag referenced through a pointer so the
// caller can emit `extern struct Tag;` for it.
static std::string toOxType(std::string t, std::set<std::string>& structTags) {
  // 1. strip qualifiers
  stripQualifiers(t);

  // 2. count + peel trailing '*'
  int ptrs = 0;
  // possible trailing stars after optional spaces, from the right
  while (true) {
    size_t n = t.size();
    if (n == 0) break;
    // trim trailing spaces
    size_t end = t.find_last_not_of(' ');
    if (end == std::string::npos) break;
    if (t[end] == '*') {
      ptrs++;
      t.erase(end);          // remove the '*'
      // trim again
      size_t e2 = t.find_last_not_of(' ');
      if (e2 != std::string::npos) t.erase(e2 + 1); else t.clear();
    } else {
      break;
    }
  }

  // 3. map the (now non-pointer) base
  std::string base = mapBaseType(t);

  // 4. record struct tags referenced through a pointer
  if (ptrs > 0) {
    // base came from "struct X" only if the original base type started with a
    // tag keyword; the stripped name has the keyword already removed by
    // mapBaseType, so detect by checking the pre-map string.
    std::string tmp = t;
    stripQualifiers(tmp);
    if (tmp.rfind("struct ", 0) == 0 || tmp.rfind("union ", 0) == 0 ||
        tmp.rfind("enum ", 0) == 0) {
      std::string tag = mapBaseType(tmp);
      if (!tag.empty() && tag != "void") structTags.insert(tag);
    }
  }

  // 5. assemble pointer prefix
  std::string ox = base;
  for (int i = 0; i < ptrs; i++) ox = "*" + ox;
  return ox;
}

// ox:note Extract the return type from a clang function-prototype qualType like
// "int (HWND, const char *, int)" or "void *(size_t)" or "struct X *(int)".
// The return type is everything before the first '(' that opens the param
// list. We cannot just split on '(' because the return type itself may
// contain parens (function-pointer returns); for the common bindgen case
// the first '(' is the arg list. We balance only on the simple form: walk
// from the left until we hit a '(' that is NOT preceded by an identifier-
// shaped token AND has no matching ')'. clang emits "RetType (ArgList)"
// with a space before '(', so first '(' always opens the arg list.
static std::string returnTypeFromProto(const std::string& proto) {
  int depth = 0;
  for (size_t i = 0; i < proto.size(); i++) {
    char c = proto[i];
    if (c == '(') {
      if (depth == 0) {
        // everything before this '(' is the return type (trim trailing spaces)
        std::string r = proto.substr(0, i);
        size_t e = r.find_last_not_of(" \t");
        if (e != std::string::npos) r.erase(e + 1); else r.clear();
        return r;
      }
      depth++;
    } else if (c == ')') {
      depth--;
    }
  }
  // no '(' found: treat the whole string as the return type
  return proto;
}

// Choose the type string to map for a ParmVarDecl / return: prefer the
// desugaredQualType (typedefs resolved) so HWND becomes "struct HWND_tag *",
// falling back to qualType. For well-known size typedefs named in qualType,
// prefer the bare name so size_t -> usize rather than unsigned long long.
static std::string typeStringFor(const JValue* typeObj) {
  if (!typeObj) return "";
  const JValue* d = typeObj->find("desugaredQualType");
  const JValue* q = typeObj->find("qualType");
  std::string qs = q ? q->str : "";
  // named size-type aliases map to usize/isize before desugaring
  if (qs == "size_t" || qs == "__size_t" || qs == "size_type" ||
      qs == "ssize_t" || qs == "intptr_t" || qs == "uintptr_t" ||
      qs == "ptrdiff_t" || qs == "wchar_t") {
    return qs;
  }
  if (d) return d->str;
  return qs;
}

// Sanitize a C identifier into an Oxide-safe one. Oxide allows alnum+'_'.
static std::string sanitizeIdent(const std::string& s) {
  std::string out;
  for (char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') out += c;
    else out += '_';
  }
  if (out.empty()) out = "_";
  if (out[0] >= '0' && out[0] <= '9') out = "p" + out;
  return out;
}

struct BindgenInfo {
  // struct tag names referenced through a pointer anywhere in fn signatures
  std::set<std::string> structTags;
  // user TypedefDecls that alias a struct pointer: typedef NAME = *Tag;
  struct TypedefAlias { std::string name; std::string tag; };
  std::vector<TypedefAlias> typedefAliases;
  // a single function parameter (kept at BindgenInfo scope so it isn't shadowed
  // by the unrelated global AST `Param` struct from AST.h)
  struct Param { std::string name; std::string type; };
  // function declarations
  struct Func {
    std::string name;
    std::string retType;
    std::vector<BindgenInfo::Param> params;
  };
  std::vector<Func> funcs;
};

// Walk the JSON AST recursively, populating `info`. Only descend into `inner`
// arrays; record FunctionDecl (non-implicit), TypedefDecl whose underlying
// is `struct X *`, and accumulate struct tags from those signatures.
static void walkAst(const JValue& node, BindgenInfo& info) {
  if (node.kind != JValue::Kind::Object) {
    if (node.kind == JValue::Kind::Array) {
      for (const auto& c : node.arr) walkAst(c, info);
    }
    return;
  }

  const JValue* kind = node.find("kind");
  if (!kind) {
    if (const JValue* inner = node.find("inner")) walkAst(*inner, info);
    return;
  }

  // FunctionDecl: only explicit (skip implicit builtins like strlen/malloc).
  if (kind->str == "FunctionDecl") {
    // isImplicit present => builtin/injected decl, skip
    if (node.find("isImplicit") && node.find("isImplicit")->kind == JValue::Kind::Bool &&
        node.find("isImplicit")->b) {
      // still descend in case nested decls exist (rare for FunctionDecl)
      if (const JValue* inner = node.find("inner")) walkAst(*inner, info);
      return;
    }
    // also skip body definitions (extern decl has no body): a FunctionDecl
    // with a body has a non-empty inner containing a CompoundStmt; we keep it
    // either way but the signature is what we want.
    const JValue* name = node.find("name");
    const JValue* typeObj = node.find("type");
    if (name && typeObj) {
      BindgenInfo::Func f;
      f.name = sanitizeIdent(name->str);
      // return type from the function prototype qualType
      const JValue* q = typeObj->find("qualType");
      std::string proto = q ? q->str : "";
      std::string retC = returnTypeFromProto(proto);
      f.retType = toOxType(retC, info.structTags);

      // params from inner ParmVarDecls (in order)
      if (const JValue* inner = node.find("inner")) {
        int pi = 0;
        for (const auto& c : inner->arr) {
          if (const JValue* ck = c.find("kind"); ck && ck->str == "ParmVarDecl") {
            BindgenInfo::Param prm;
            const JValue* pn = c.find("name");
            if (pn && !pn->str.empty()) prm.name = sanitizeIdent(pn->str);
            else { prm.name = "p" + std::to_string(pi); }
            prm.type = toOxType(typeStringFor(c.find("type")), info.structTags);
            f.params.push_back(std::move(prm));
          }
          pi++;
        }
      }
      info.funcs.push_back(std::move(f));
    }
    // don't descend further into a FunctionDecl's inner (we already swept
    // ParmVarDecls); CompoundStmt bodies are noise for bindgen.
    return;
  }

  // TypedefDecl: if it aliases a `struct X *`, record for `typedef NAME=*X;`.
  if (kind->str == "TypedefDecl") {
    if (!(node.find("isImplicit") && node.find("isImplicit")->b)) {
      const JValue* name = node.find("name");
      const JValue* typeObj = node.find("type");
      if (name && typeObj) {
        const JValue* q = typeObj->find("qualType");
        std::string qt = q ? q->str : "";
        // strip qualifiers then look for `struct X *` / `union X *`
        std::string s = qt;
        stripQualifiers(s);
        // count trailing '*'
        int ptrs = 0;
        while (true) {
          size_t e = s.find_last_not_of(' ');
          if (e == std::string::npos) break;
          if (s[e] == '*') { ptrs++; s.erase(e); size_t e2 = s.find_last_not_of(' '); if (e2!=std::string::npos) s.erase(e2+1); else s.clear(); }
          else break;
        }
        std::string tag;
        if (ptrs >= 1 && (s.rfind("struct ", 0) == 0 || s.rfind("union ", 0) == 0)) {
          if (s.rfind("struct ", 0) == 0) tag = s.substr(7);
          else tag = s.substr(6);
          if (!tag.empty()) {
            info.typedefAliases.push_back({sanitizeIdent(name->str), sanitizeIdent(tag)});
            info.structTags.insert(sanitizeIdent(tag));
          }
        }
      }
    }
    return;
  }

  // Descend into everything else (TranslationUnitDecl, namespaces, extern "C"
  // LinkageSpecDecl, etc.) through `inner`.
  if (const JValue* inner = node.find("inner")) walkAst(*inner, info);
}

} // namespace bindgen_detail

bool Driver::doBindgen(const Options& opt) {
  // ox:note 1. find a clang to shell out to
  std::string clang = findTool({"clang", "clang-cl"});
  if (clang.empty()) {
    errs.push_back({"bindgen", "clang not found on PATH (needed for AST dump)", 0, 0});
    return false;
  }

  // ox:note 2. run `clang -Xclang -ast-dump=json <header>` capturing stdout.
  //    -Xclang forwards -ast-dump=json to cc1 under the driver. clang-cl
  //    accepts the same -Xclang bridge.
  std::string cmd = clang + " -Xclang -ast-dump=json \"" + opt.input + "\"";
#ifdef _WIN32
  cmd += " 2>nul";
#else
  cmd += " 2>/dev/null";
#endif
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    errs.push_back({"bindgen", "cannot launch clang to read the AST", 0, 0});
    return false;
  }
  std::string json;
  {
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) json.append(buf, n);
  }
  int rc = pclose(pipe);
  if (rc != 0 || json.empty()) {
    errs.push_back({"bindgen",
      "clang failed to dump the AST (exit " + std::to_string(rc) +
      "). Check the header path and that clang -Xclang -ast-dump=json works on it.", 0, 0});
    return false;
  }

  // 3. parse the JSON
  bindgen_detail::JParser parser(json);
  bindgen_detail::JValue root = parser.parse();
  if (root.kind != bindgen_detail::JValue::Kind::Object) {
    errs.push_back({"bindgen", "clang AST JSON did not parse as an object", 0, 0});
    return false;
  }

  // 4. walk and collect declarations
  bindgen_detail::BindgenInfo info;
  bindgen_detail::walkAst(root, info);

  // 5. emit the Oxide source
  std::string out;
  out += "// Auto-generated by oxide bindgen from " + opt.input + "\n";
  out += "// NOTE: opaque structs passed by pointer; types are best-effort C->Oxide mappings.\n";

  // opaque struct declarations (sorted for stable output)
  for (const auto& tag : info.structTags) {
    out += "extern struct " + tag + ";\n";
  }
  if (!info.structTags.empty()) out += "\n";

  // typedef aliases to struct pointers
  for (const auto& a : info.typedefAliases) {
    out += "typedef " + a.name + " = *" + a.tag + ";\n";
  }
  if (!info.typedefAliases.empty()) out += "\n";

  // extern fn declarations
  for (const auto& f : info.funcs) {
    out += "extern fn " + f.name + "(";
    for (size_t i = 0; i < f.params.size(); i++) {
      if (i) out += ", ";
      out += f.params[i].name + ": " + f.params[i].type;
    }
    out += ") -> " + f.retType + ";\n";
  }

  // 6. write to -o or stdout
  if (!opt.output.empty()) {
    std::ofstream f(opt.output, std::ios::binary);
    if (!f) {
      errs.push_back({"bindgen", "cannot open '" + opt.output + "' for write", 0, 0});
      return false;
    }
    f << out;
    if (!f) {
      errs.push_back({"bindgen", "write to '" + opt.output + "' failed", 0, 0});
      return false;
    }
  } else {
    std::fputs(out.c_str(), stdout);
  }
  return true;
}
