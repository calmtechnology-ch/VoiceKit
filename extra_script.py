import os
Import("env")

lib_dir = Dir(".").srcnode().abspath
inc_dir = os.path.join(lib_dir, "src", "esp_sr", "include")
lib_path = os.path.join(lib_dir, "src", "esp_sr", "lib", "esp32s3")

if os.path.isdir(inc_dir):
    env.Append(CPPPATH=[inc_dir])

if os.path.isdir(lib_path):
    env.Append(LIBPATH=[lib_path])
    env.Append(LIBS=["esp_sr_wakenet_s3"])

