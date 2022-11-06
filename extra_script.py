Import("env")
#Import("shutil")

print("Current CLI targets", COMMAND_LINE_TARGETS)
print("Current Build targets", BUILD_TARGETS)

def pre_program_action(source, target, env):
    print("ACHTUNG : Program will be built!")

def post_program_action(source, target, env):
    print("Program has been built!")
    program_path = target[0].get_abspath()
    print("Program path", program_path)
    # Use case: sign a firmware, do any manipulations with ELF, etc
    # env.Execute(f"sign --elf {program_path}")

env.AddPostAction("$PROGPATH", post_program_action)

env.AddPreAction("$PROGPATH", pre_program_action)

#
# Upload actions
#

def before_upload(source, target, env):
    print("before_upload")
    # do some actions

    # call Node.JS or other script
    #env.Execute("node --version")


def after_upload(source, target, env):
    print("after_upload")
    #shutil.rmtree("C:\\Users\\alban.WARREN\\Documents\\PlatformIO\\Projects\\simpleThermostatReplacement\\.pio\\libdeps\\nodemcuv09\\Homie")
    # do some actions

env.AddPreAction("upload", before_upload)
env.AddPostAction("upload", after_upload)

#
# Custom actions when building program/firmware
#

#env.AddPreAction("buildprog", callback...)
#env.AddPostAction("buildprog", callback...)

#
# Custom actions for specific files/objects
#

#env.AddPreAction("$PROGPATH", callback...)
#env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", [callback1, callback2,...])
#env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", callback...)

# custom action before building SPIFFS image. For example, compress HTML, etc.
#env.AddPreAction("$BUILD_DIR/spiffs.bin", callback...)

# custom action for project's main.cpp
#env.AddPostAction("$BUILD_DIR/src/main.cpp.o", callback...)

# Custom HEX from ELF
#env.AddPostAction(
#    "$BUILD_DIR/${PROGNAME}.elf",
#    env.VerboseAction(" ".join([
#        "$OBJCOPY", "-O", "ihex", "-R", ".eeprom",
#        "$BUILD_DIR/${PROGNAME}.elf", "$BUILD_DIR/${PROGNAME}.hex"
#    ]), "Building $BUILD_DIR/${PROGNAME}.hex")
#)