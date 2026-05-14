# Trusted-installer-shell - minimum possible size
Launches a command prompt with trusted installer permissions, but only 3.5 KB in size. 

---

Basically, you have to do some voodoo shit in the project config to get it done properly. But trust me, you don't want to go through that. So, just download from the assets the small executable; if it malfunctions, I'm not to blame; this is just a leisure project. 

Here is the voodoo shit: 

* **Windows SDK Version:** `10.0` 

* **VC Project Version:** `18.0` 

* **Platform Toolset:** `v145` (Visual Studio 2022) 
---

### **C++ Compiler Settings (`Clang/MSVC`)**

* **Language Standard:** `ISO C++20 Standard (/std:c++20)` 

* **Warning Level:** `Level 3` 

* **SDL Checks:** `Enabled` 

* **Conformance Mode:** `Enabled` 

* **Buffer Security Check:** `Disabled` (specifically for x64 configurations) 

* **Optimization Features:** `Intrinsic Functions` and `Function-Level Linking` are enabled in Release modes.



---

### **Linker & Binary Settings**

* **Subsystem:** `Console` 

* **Entry Point:** `wmainCRTStartup` 

* **Generate Debug Info:** `Disabled` for x64 

* **Ignore All Default Libraries:** `True` (You're playing dangerous games here) 

* **Section Alignment:** `/ALIGN:16` 

* **Manifest Generation:** `Disabled` 

* **Security Mitigations:** `ASLR` (Randomised Base Address) and `DEP` (Data Execution Prevention) are both **Disabled** for x64.

* **Fixed Base Address:** `True` 





If you want me to fix something, don't leave an issue, fork it and fix it, idc.

**Don't run it on anything below Windows XP; Trusted Installer didn't exist back then.** 

*I only compiled it with VS 18 and the latest Windows SDK, but apparently the ones I mentioned are supported*

*Download the C++ file and compile it with Visual Studio, not that complicated, and if you have issues, ask AI.*
## If you brick your OS with this, that's not my fault; you are responsible for that, not me. 
* **I mean, if you can get this to compile, you know better than to** `rd /s /q C:\Windows`

## My final words: compile this if you hate yourself, and if the compiler crashes out, don't blame me. 

**One last thing: do yourself a favour and download the precompiled.** 
