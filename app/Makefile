SHELL := /bin/bash
.DELETE_ON_ERROR:

JAVA_HOME ?= $(HOME)/opt/jdk17/jdk-17.0.21+9
ANDROID_HOME ?= $(HOME)/opt/android-sdk
BUILD_TOOLS ?= $(ANDROID_HOME)/build-tools/34.0.0
PLATFORM_JAR ?= $(ANDROID_HOME)/platforms/android-34/android.jar

export PATH := $(JAVA_HOME)/bin:$(PATH)

JAVAC := $(JAVA_HOME)/bin/javac
KEYTOOL := $(JAVA_HOME)/bin/keytool
D8 := $(BUILD_TOOLS)/d8
AAPT2 := $(BUILD_TOOLS)/aapt2
ZIPALIGN := $(BUILD_TOOLS)/zipalign
APKSIGNER := $(BUILD_TOOLS)/apksigner
ZIP := /usr/bin/zip

BUILD := build
CLASSES := $(BUILD)/classes
DEX := $(BUILD)/dex
UNSIGNED := $(BUILD)/PD2055Charge-unsigned.apk
ALIGNED := $(BUILD)/PD2055Charge-aligned.apk
APK := ../build/PD2055Charge.apk
KEYSTORE ?= $(HOME)/.android/debug.keystore
KEY_ALIAS ?= androiddebugkey
KEY_PASS ?= android
SOURCES := $(shell find src -name '*.java')

.PHONY: all clean check
all: $(APK)

../build:
	mkdir -p $@

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/classes.stamp: $(SOURCES) AndroidManifest.xml | $(BUILD)
	@test -d "$(BUILD_TOOLS)" || { echo "missing Android build-tools: $(BUILD_TOOLS)"; exit 1; }
	rm -rf $(CLASSES) $(DEX)
	mkdir -p $(CLASSES) $(DEX)
	$(JAVAC) -encoding UTF-8 -source 1.8 -target 1.8 \
		-bootclasspath "$(PLATFORM_JAR)" \
		-d $(CLASSES) $(SOURCES)
	touch $@

$(DEX)/classes.dex: $(BUILD)/classes.stamp
	$(D8) --release --min-api 29 --lib "$(PLATFORM_JAR)" \
		--output $(DEX) $$(find $(CLASSES) -name '*.class')

$(UNSIGNED): $(DEX)/classes.dex AndroidManifest.xml | $(BUILD)
	$(AAPT2) link -o $@ -I "$(PLATFORM_JAR)" --manifest AndroidManifest.xml
	cd $(BUILD) && $(ZIP) -q -j PD2055Charge-unsigned.apk dex/classes.dex

$(KEYSTORE):
	mkdir -p $(dir $@)
	$(KEYTOOL) -genkeypair -v -keystore $@ -alias $(KEY_ALIAS) \
		-keyalg RSA -keysize 2048 -validity 10000 \
		-storepass $(KEY_PASS) -keypass $(KEY_PASS) \
		-dname "CN=Android Debug,O=Android,C=US"

$(ALIGNED): $(UNSIGNED) | $(BUILD)
	$(ZIPALIGN) -f 4 $< $@

$(APK): $(ALIGNED) $(KEYSTORE) | ../build
	$(APKSIGNER) sign --ks $(KEYSTORE) --ks-key-alias $(KEY_ALIAS) \
		--ks-pass pass:$(KEY_PASS) --key-pass pass:$(KEY_PASS) \
		--out $@ $<

check: $(APK)
	$(APKSIGNER) verify --verbose --print-certs $(APK)
	$(BUILD_TOOLS)/aapt2 dump badging $(APK) | head -8

clean:
	rm -rf $(BUILD)
	rm -f $(APK)
