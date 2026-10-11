set_project("dirhop")
set_version("0.2.0")
set_languages("c11")
set_warnings("all", "extra")
set_optimize("smallest")
set_strip("all")

target("dirhop")
    set_kind("binary")
    -- Package target stays dirhop. The installed executable is dho.
    set_basename("dho")
    -- xmake otherwise links with g++, which pulls libstdc++.
    set_toolset("ld", "gcc")
    add_includedirs("src")
    add_files("src/*.c")
    add_defines("_DEFAULT_SOURCE", "_XOPEN_SOURCE=700")
    -- C needs no unwind tables, and they are a sixth of the binary.
    add_cflags("-Os", "-Wall", "-Wextra", "-flto",
               "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", {force = true})
    add_ldflags("-flto", "-s")
    if is_plat("linux") then
        -- Without this, GNU ld pads each segment to its own 4 KB page.
        add_ldflags("-Wl,-z,noseparate-code", {force = true})
    end
    add_installfiles("shell/(dirhop.bash)", {prefixdir = "share/dirhop"})
    add_installfiles("shell/(dirhop.fish)", {prefixdir = "share/dirhop"})
