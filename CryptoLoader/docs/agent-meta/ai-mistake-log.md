# AI Mistake Log

Tool misuse/wrong path -> Used `CryptoLoader/dllmain.cpp` while already in `CryptoLoader` cwd -> Re-check cwd and validate path with `Test-Path` before scan commands.
