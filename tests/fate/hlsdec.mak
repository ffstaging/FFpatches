# Rewrite an HLS playlist to use a data: URI for the AES-128 key.
# The key file contains the ASCII string "0000000000000000" (16 bytes of 0x30),
# which base64-encodes to MDAwMDAwMDAwMDAwMDAwMA==.
tests/data/hls_data_uri.m3u8: TAG = GEN
tests/data/hls_data_uri.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=2" -f hls -hls_time 1 -map 0 \
	-flags +bitexact -codec:a mp2fixed -hls_list_size 0 \
	-hls_enc 1 -hls_enc_key 00000000000000000000000000000000 \
	-hls_enc_iv 00000000000000000000000000000000 \
	-hls_segment_filename $(TARGET_PATH)/tests/data/hls_data_uri_%d.ts \
	$(TARGET_PATH)/tests/data/hls_data_uri.m3u8 2>/dev/null; \
	sed -i 's|URI="[^"]*"|URI="data:application/octet-stream;base64,MDAwMDAwMDAwMDAwMDAwMA=="|' \
	$(TARGET_PATH)/tests/data/hls_data_uri.m3u8

FATE_HLSDEC-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV CRYPTO_PROTOCOL DATA_PROTOCOL) += fate-hls-data-uri
fate-hls-data-uri: tests/data/hls_data_uri.m3u8
fate-hls-data-uri: CMD = framecrc -auto_conversion_filters -flags +bitexact -i $(TARGET_PATH)/tests/data/hls_data_uri.m3u8

FATE_HLSDEC-yes := $(if $(call FRAMECRC), $(FATE_HLSDEC-yes))

FATE_FFMPEG += $(FATE_HLSDEC-yes)
fate-hlsdec: $(FATE_HLSDEC-yes)
