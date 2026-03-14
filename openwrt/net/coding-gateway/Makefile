include $(TOPDIR)/rules.mk

PKG_NAME:=coding-gateway
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/coding-gateway
  SECTION:=net
  CATEGORY:=Network
  TITLE:=RLNC erasure-coding multi-path gateway
  DEPENDS:=
  MAINTAINER:=coding-gateway contributors
endef

define Package/coding-gateway/description
  Resilient multi-path transmission for millimeter-wave networks through
  erasure coding. Transparent L3 tunnel using Random Linear Network Coding
  (RLNC) over GF(2^8) with adaptive redundancy across multiple UDP paths.
  Zero external dependencies.
endef

define Package/coding-gateway/conffiles
/etc/coding-gateway/coding-gateway.conf
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(CURDIR)/../../../src $(PKG_BUILD_DIR)/
	$(CP) $(CURDIR)/../../../include $(PKG_BUILD_DIR)/
	$(CP) $(CURDIR)/../../../Makefile $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -std=c99 -Wall -Wextra -Wpedantic \
			-Iinclude -D_POSIX_C_SOURCE=200112L" \
		LDFLAGS="$(TARGET_LDFLAGS) -lm" \
		clean all
endef

define Package/coding-gateway/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/coding-gateway $(1)/usr/sbin/coding-gateway

	$(INSTALL_DIR) $(1)/etc/coding-gateway
	$(INSTALL_CONF) ./files/coding-gateway.conf $(1)/etc/coding-gateway/coding-gateway.conf

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/coding-gateway.init $(1)/etc/init.d/coding-gateway
endef

$(eval $(call BuildPackage,coding-gateway))
