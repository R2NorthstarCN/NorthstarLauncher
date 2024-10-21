#include <openssl/evp.h>
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <fstream>
#include <libloaderapi.h>



std::string md5(char* buffer,size_t size)
{
  EVP_MD_CTX*   context = EVP_MD_CTX_new();
  const EVP_MD* md = EVP_md5();
  unsigned char md_value[EVP_MAX_MD_SIZE];
  unsigned int  md_len;
  std::string        output;

  EVP_DigestInit_ex2(context, md, NULL);
  EVP_DigestUpdate(context, buffer, size);
  EVP_DigestFinal_ex(context, md_value, &md_len);
  EVP_MD_CTX_free(context);

  output.resize(md_len * 2);
  for (unsigned int i = 0 ; i < md_len ; ++i)
    std::sprintf(&output[i * 2], "%02x", md_value[i]);
  return output;
}

std::string FileToMD5(char * filename){
    std::string md5string = "";
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (file.read(buffer.data(), size))
    {
        md5string = md5(buffer.data(),size);
    }
    return md5string;
}

ON_DLL_LOAD("XianFanOverlayDx11Layer64.dll", XGUNOverlaySignatureCheck, (CModule module))
{   
    char filename[512];
    std::string md5string;
    auto ret = GetModuleFileNameA((HMODULE)module.GetModuleBase(),filename,512);
    md5string = FileToMD5(filename);

    if(!md5string.empty() && ("a12ff55875e4c81bd8d3a6ae9aec3338" == md5string))
    {
        spdlog::info("[XGUN] Loading XGUN Overlay DLL with Signature: {} ", md5string);
    }else
    {
        spdlog::warn("[XGUN] Unsupported or Unknown XGUN Overlay DLL Loaded! Signature:{}",md5string);
        MessageBoxW(nullptr,
			L"正在运行的XGUN模块可能与当前版本的北极星CN不支持，或已损坏。游戏与XGUN模块可能不会正常工作。",
			L"警告: 无法验证XGUN兼容性",
			MB_OK | MB_ICONWARNING);
    }
    
}
