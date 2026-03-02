FATE_VORBISCOMMENT-$(call ENCMUX, FLAC, OGG, FLAC_DECODER OGG_DEMUXER LAVFI_INDEV) += fate-vorbiscomment-meta
fate-vorbiscomment-meta: CMD = md5 -f lavfi -i "sine=frequency=440:duration=1" \
    -metadata title=lavftest -metadata artist=testartist \
    -c:a flac -f ogg -flags +bitexact -fflags +bitexact

FATE_VORBISCOMMENT-$(call ENCMUX, FLAC, OGG, FLAC_DECODER OGG_DEMUXER LAVFI_INDEV) += fate-vorbiscomment-no-meta
fate-vorbiscomment-no-meta: CMD = md5 -f lavfi -i "sine=frequency=440:duration=1" \
    -map_metadata -1 \
    -c:a flac -f ogg -flags +bitexact -fflags +bitexact

FATE_VORBISCOMMENT-$(call ALLYES, OGG_MUXER OPUS_DECODER OGG_DEMUXER LAVFI_INDEV FFMETADATA_DEMUXER ARESAMPLE_FILTER) += fate-vorbiscomment-chapters
fate-vorbiscomment-chapters: CMD = md5 \
    -f lavfi -i "sine=frequency=440:duration=4" \
    -f ffmetadata -i $(SRC_PATH)/tests/vorbiscomment-chapters.ffmeta \
    -map 0:a -map_metadata 1 -map_chapters 1 \
    -af aresample=48000 -c:a opus -strict -2 -f ogg -flags +bitexact -fflags +bitexact

FATE_FFMPEG += $(FATE_VORBISCOMMENT-yes)
fate-vorbiscomment: $(FATE_VORBISCOMMENT-yes)
