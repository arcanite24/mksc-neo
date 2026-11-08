// Offline DXC command adapter for the pinned macOS library. Never linked into mksc.
#include <dxc/dxcapi.h>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    void *library = dlopen(DXC_LIBRARY, RTLD_NOW | RTLD_LOCAL);
    if (!library) { std::cerr << dlerror() << '\n'; return 1; }
    auto create = reinterpret_cast<DxcCreateInstanceProc>(dlsym(library, "DxcCreateInstance"));
    IDxcCompiler3 *compiler = nullptr;
    if (!create || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) return 1;
    if (argc == 2 && std::string(argv[1]) == "--version") {
        IDxcVersionInfo2 *version = nullptr;
        if (FAILED(compiler->QueryInterface(IID_PPV_ARGS(&version)))) return 1;
        UINT32 count = 0; char *hash = nullptr;
        if (FAILED(version->GetCommitInfo(&count, &hash))) return 1;
        std::cout << "DXC " << hash << '\n';
        return 0;
    }
    if (argc < 3) return 1;
    std::ifstream source(argv[1], std::ios::binary);
    if (!source) return 1;
    std::string text((std::istreambuf_iterator<char>(source)), {}), output;
    std::vector<std::wstring> arguments;
    for (int i=2; i<argc; ++i) {
        if (std::string(argv[i]) == "-Fo" && i+1<argc) { output=argv[++i]; continue; }
        std::string a(argv[i]); arguments.emplace_back(a.begin(),a.end());
    }
    std::vector<LPCWSTR> pointers;
    for (const auto &a:arguments) pointers.push_back(a.c_str());
    DxcBuffer buffer={text.data(),text.size(),DXC_CP_UTF8};
    IDxcResult *result = nullptr;
    if (FAILED(compiler->Compile(&buffer,pointers.data(),static_cast<UINT32>(pointers.size()),nullptr,IID_PPV_ARGS(&result)))) return 1;
    IDxcBlobUtf8 *errors = nullptr;
    result->GetOutput(DXC_OUT_ERRORS,IID_PPV_ARGS(&errors),nullptr);
    if (errors && errors->GetStringLength()) std::cerr << errors->GetStringPointer();
    HRESULT status;
    if (FAILED(result->GetStatus(&status)) || FAILED(status)) return 1;
    IDxcBlob *object = nullptr;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT,IID_PPV_ARGS(&object),nullptr)) || output.empty()) return 1;
    std::ofstream file(output,std::ios::binary);
    file.write(static_cast<const char *>(object->GetBufferPointer()),object->GetBufferSize());
    return file ? 0 : 1;
}
