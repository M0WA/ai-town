# Licenses

## Project Notice

**AI Town is a private, non-commercial, educational-only project.**
It is not distributed for sale or commercial gain. All third-party assets
are used strictly within the terms of their respective licenses for
non-commercial purposes.

---

## AI Town Source Code

The AI Town source code (all files under `src/`, `tools/`, `.github/`, and
configuration files at the repository root) is proprietary and all rights
reserved unless otherwise stated.

---

## Third-Party Assets — Producer.ai Music

The following music tracks were generated using [Producer.ai](https://producer.ai)
and are used **for non-commercial purposes only** in accordance with
Producer.ai's free non-commercial use terms.

**Attribution**: Music generated with Producer.ai (<https://producer.ai>)

| Asset file                                              | Description                        |
| ------------------------------------------------------- | ---------------------------------- |
| `assets/audio/producer.ai/music/music_calm_01.wav`      | Calm ambient track, variant 1      |
| `assets/audio/producer.ai/music/music_calm_02.wav`      | Calm ambient track, variant 2      |
| `assets/audio/producer.ai/music/music_crisis_01.wav`    | Crisis/tension track, variant 1    |
| `assets/audio/producer.ai/music/music_crisis_02.wav`    | Crisis/tension track, variant 2    |
| `assets/audio/producer.ai/music/music_growth_01.wav`    | Growth/prosperity track, variant 1 |
| `assets/audio/producer.ai/music/music_growth_02.wav`    | Growth/prosperity track, variant 2 |
| `assets/audio/producer.ai/music/music_main_menu_01.wav` | Main menu theme, variant 1         |
| `assets/audio/producer.ai/music/music_main_menu_02.wav` | Main menu theme, variant 2         |

These tracks are used solely within this private, non-commercial,
educational-only project. **Commercial use is not permitted.**

---

## Third-Party Assets — Google Fonts (SIL Open Font License 1.1)

The following typefaces are used for HUD bitmap font rendering and are
distributed under the **SIL Open Font License, Version 1.1** (OFL-1.1).
The full license text is available at <https://scripts.sil.org/OFL>.

### Barlow Condensed

- **Author**: Jeremy Tribby
- **Source**: <https://fonts.google.com/specimen/Barlow+Condensed>
- **License**: SIL Open Font License 1.1
- **Files**: `assets/fonts/src/BarlowCondensed-*.ttf`

### Share Tech Mono

- **Author**: Ralph du Carrois (Carrois Type Design)
- **Source**: <https://fonts.google.com/specimen/Share+Tech+Mono>
- **License**: SIL Open Font License 1.1
- **Files**: `assets/fonts/src/ShareTechMono-Regular.ttf`

---

## Third-Party Libraries — Irrlicht Engine (zlib License)

Irrlicht is the 3D rendering engine used by AI Town. On Windows it is distributed
as `Irrlicht.dll` inside the installer. On Linux it is a runtime dependency resolved
via the system package manager.

**Copyright (C) 2002-2015 Nikolaus Gebhardt**

> This software is provided 'as-is', without any express or implied warranty. In no
> event will the authors be held liable for any damages arising from the use of this
> software.
>
> Permission is granted to anyone to use this software for any purpose, including
> commercial applications, and to alter it and redistribute it freely, subject to the
> following restrictions:
>
> 1. The origin of this software must not be misrepresented; you must not claim that
>    you wrote the original software. If you use this software in a product, an
>    acknowledgement in the product documentation would be appreciated but is not
>    required.
> 2. Altered source versions must be clearly marked as such, and must not be
>    misrepresented as being the original software.
> 3. This notice may not be removed or altered from any source distribution.

- **Source**: <https://irrlicht.sourceforge.io>
- **License**: zlib/libpng License

---

## Third-Party Libraries — OpenAL Soft (GNU Lesser General Public License 2.1)

OpenAL Soft is the audio engine used by AI Town for 3D spatial audio. On Windows
it is distributed as `soft_oal.dll` inside the installer. On Linux it is a runtime
dependency (`libopenal1`) resolved via the system package manager.

**LGPL Relinking Notice**: In accordance with the GNU LGPL, users of the Windows
installer may replace `soft_oal.dll` with a modified version of OpenAL Soft compiled
from its source code (available at <https://github.com/kcat/openal-soft>) without
any further restriction. The remainder of AI Town is dynamically linked to OpenAL
Soft exclusively through this DLL boundary.

- **Copyright (C)** The OpenAL Soft contributors
- **Source**: <https://github.com/kcat/openal-soft>
- **License**: GNU Library General Public License, Version 2 (LGPL-2.0-or-later)
- **Full license text**: <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>

---

## Third-Party Libraries — libvorbis and libogg (BSD 3-Clause License)

libvorbis and its dependency libogg are used by AI Town for OGG audio decoding.
On Windows they are distributed as DLLs inside the installer. On Linux they are
runtime dependencies (`libvorbis0a`) resolved via the system package manager.

**libvorbis**

> Copyright (c) 2002-2020 Xiph.org Foundation
>
> Redistribution and use in source and binary forms, with or without modification,
> are permitted provided that the following conditions are met:
>
> - Redistributions of source code must retain the above copyright notice, this list
>   of conditions and the following disclaimer.
> - Redistributions in binary form must reproduce the above copyright notice, this
>   list of conditions and the following disclaimer in the documentation and/or other
>   materials provided with the distribution.
> - Neither the name of the Xiph.org Foundation nor the names of its contributors
>   may be used to endorse or promote products derived from this software without
>   specific prior written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
> ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
> WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
> IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
> INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
> BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
> DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
> LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
> OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
> OF THE POSSIBILITY OF SUCH DAMAGE.

**libogg**

> Copyright (c) 2002, Xiph.org Foundation
>
> Redistribution and use in source and binary forms, with or without modification,
> are permitted provided that the following conditions are met:
>
> - Redistributions of source code must retain the above copyright notice, this list
>   of conditions and the following disclaimer.
> - Redistributions in binary form must reproduce the above copyright notice, this
>   list of conditions and the following disclaimer in the documentation and/or other
>   materials provided with the distribution.
> - Neither the name of the Xiph.org Foundation nor the names of its contributors
>   may be used to endorse or promote products derived from this software without
>   specific prior written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
> ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
> WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
> IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
> INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
> BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
> DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
> LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
> OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
> OF THE POSSIBILITY OF SUCH DAMAGE.

- **Source**: <https://xiph.org/vorbis/> / <https://xiph.org/ogg/>
- **License**: BSD 3-Clause License

---

## Third-Party Libraries — GLEW (Multi-License)

The OpenGL Extension Wrangler Library (GLEW) is used by AI Town for OpenGL
extension loading. On Windows it is distributed as `GLEW32.dll` inside the
installer. On Linux it is a runtime dependency resolved via the system package
manager.

**GLEW core — Modified BSD**

> Copyright (C) 2002-2007, Milan Ikits \<milan ikits[]ieee org\>
> Copyright (C) 2002-2007, Marcelo E. Magallon \<mmagallo[]debian org\>
> Copyright (C) 2002, Lev Povalahev
> All rights reserved.
>
> Redistribution and use in source and binary forms, with or without modification,
> are permitted provided that the following conditions are met:
>
> - Redistributions of source code must retain the above copyright notice, this list
>   of conditions and the following disclaimer.
> - Redistributions in binary form must reproduce the above copyright notice, this
>   list of conditions and the following disclaimer in the documentation and/or other
>   materials provided with the distribution.
> - The name of the author may be used to endorse or promote products derived from
>   this software without specific prior written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
> ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
> WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
> IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
> INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
> BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
> DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
> LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
> OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
> OF THE POSSIBILITY OF SUCH DAMAGE.

**Mesa 3-D graphics library portions — MIT**

> Copyright (C) 1999-2007 Brian Paul. All Rights Reserved.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of
> this software and associated documentation files (the "Software"), to deal in the
> Software without restriction, including without limitation the rights to use, copy,
> modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
> and to permit persons to whom the Software is furnished to do so, subject to the
> following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
> FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL BRIAN PAUL BE
> LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
> CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
> SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**Khronos OpenGL headers — MIT**

> Copyright (c) 2007 The Khronos Group Inc.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of
> this software and/or associated documentation files (the "Materials"), to deal in
> the Materials without restriction, including without limitation the rights to use,
> copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
> Materials, and to permit persons to whom the Materials are furnished to do so,
> subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Materials.
>
> THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
> FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
> COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
> AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
> WITH THE MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.

- **Source**: <https://glew.sourceforge.net>
- **License**: Modified BSD / MIT (multi-license as above)

---

## Third-Party Libraries — {fmt} (MIT License)

{fmt} is a text formatting library compiled into the AI Town binary. On Windows a
`fmt.dll` is also distributed inside the installer.

> Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of
> this software and associated documentation files (the "Software"), to deal in the
> Software without restriction, including without limitation the rights to use, copy,
> modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
> and to permit persons to whom the Software is furnished to do so, subject to the
> following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
> FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
> COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
> AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
> WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

- **Source**: <https://github.com/fmtlib/fmt>
- **License**: MIT License

---

## Third-Party Libraries — nlohmann/json (MIT License)

nlohmann/json is a header-only JSON library compiled into the AI Town binary.

> Copyright (c) 2013-2025 Niels Lohmann
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of
> this software and associated documentation files (the "Software"), to deal in the
> Software without restriction, including without limitation the rights to use, copy,
> modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
> and to permit persons to whom the Software is furnished to do so, subject to the
> following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
> FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
> COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
> AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
> WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

- **Source**: <https://github.com/nlohmann/json>
- **License**: MIT License

---

## Third-Party Libraries — Google Test and Google Mock (BSD 3-Clause License)

Google Test (GTest) and Google Mock (GMock) are used exclusively for automated
testing and are **not distributed** in release packages (built only when
`BUILD_TESTING=ON`).

> Copyright 2008, Google Inc. All rights reserved.
>
> Redistribution and use in source and binary forms, with or without modification,
> are permitted provided that the following conditions are met:
>
> - Redistributions of source code must retain the above copyright notice, this list
>   of conditions and the following disclaimer.
> - Redistributions in binary form must reproduce the above copyright notice, this
>   list of conditions and the following disclaimer in the documentation and/or other
>   materials provided with the distribution.
> - Neither the name of Google Inc. nor the names of its contributors may be used to
>   endorse or promote products derived from this software without specific prior
>   written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
> ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
> WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
> IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
> INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
> BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
> DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
> LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
> OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
> OF THE POSSIBILITY OF SUCH DAMAGE.

- **Source**: <https://github.com/google/googletest>
- **License**: BSD 3-Clause License

---

## Third-Party Libraries — RapidCheck (BSD 2-Clause License)

RapidCheck is a property-based testing library used exclusively for automated
testing and is **not distributed** in release packages (built only when
`BUILD_TESTING=ON`).

> Copyright (c) 2014-2015, Emil Eriksson. All rights reserved.
>
> Redistribution and use in source and binary forms, with or without modification,
> are permitted provided that the following conditions are met:
>
> 1. Redistributions of source code must retain the above copyright notice, this
>    list of conditions and the following disclaimer.
> 2. Redistributions in binary form must reproduce the above copyright notice, this
>    list of conditions and the following disclaimer in the documentation and/or
>    other materials provided with the distribution.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
> ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
> WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
> IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
> INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
> BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
> DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
> LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
> OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
> OF THE POSSIBILITY OF SUCH DAMAGE.

- **Source**: <https://github.com/emil-e/rapidcheck>
- **License**: BSD 2-Clause License
