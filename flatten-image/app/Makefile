PROGS = $(shell jq -r '.acapPackageConf.setup.appName' manifest.json)
OBJECTS = flatten_image.cpp imgprovider.cpp dewarper.cpp rtsp_server.cpp panic.cpp
DEBUG_DIR = debug

PKGS = gio-2.0 gio-unix-2.0 vdostream axparameter gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0

CXXFLAGS += -Os -pipe -std=c++17
CXXFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags-only-I $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS))

CXXFLAGS += -I$(SDKTARGETSYSROOT)/usr/include/opencv4
CXXFLAGS += -I$(SDKTARGETSYSROOT)/usr/include/gstreamer-1.0
LDFLAGS = -L./lib -Wl,--no-as-needed,-rpath,'$$ORIGIN/lib'
LDLIBS += -lm -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lgstrtspserver-1.0

.PHONY: all clean

all: $(PROGS)

$(PROGS): $(OBJECTS)
	install -d $(DEBUG_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $(DEBUG_DIR)/$@
	cp $(DEBUG_DIR)/$@ .
	$(STRIP) --strip-unneeded $@

clean:
	rm -rf $(PROGS) *.o *.eap* *_LICENSE.txt package.conf* param.conf tmp* $(DEBUG_DIR)
