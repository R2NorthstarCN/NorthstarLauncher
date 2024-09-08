#include<openssl/md5.h> 
#include<stdio.h> 
#include<stdlib.h> 
#include<string.h> 
 
std::string GenerateMD5Hash(void* moduleBase, size_t moduleSize) {
    MD5_CTX md5Context;
    MD5_Init(&md5Context);
    MD5_Update(&md5Context, moduleBase, moduleSize);
    unsigned char hash[MD5_DIGEST_LENGTH];
    MD5_Final(hash, &md5Context);

    std::ostringstream oss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return oss.str();
}
ON_DLL_LOAD("engine.dll", XGUNSignatureCheck, (CModule module))
{
	std::string md5String = GenerateMD5Hash((void*)module.GetModuleBase(),module.GetModuleSize());
    spdlog::info("[XGUN] Loading XGUN DLL with Signature: {}", md5String);
}