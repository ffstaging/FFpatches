FATE_MPEGH3DA += fate-mpegh3da-decode-2-2
fate-mpegh3da-decode-2-2: CMD = ffmpeg -auto_conversion_filters -c:a libmpeghdec -ch_layout stereo -i $(TARGET_SAMPLES)/mpegh3da/mpegh_cicp_02_baseline_32kbps.mp4 -f s16le -
fate-mpegh3da-decode-2-2: REF = $(SAMPLES)/mpegh3da/reference_stereo_to_stereo.pcm

FATE_MPEGH3DA += fate-mpegh3da-decode-2-13
fate-mpegh3da-decode-2-13: CMD = ffmpeg -auto_conversion_filters -c:a libmpeghdec -ch_layout 22.2 -i $(TARGET_SAMPLES)/mpegh3da/mpegh_cicp_02_baseline_32kbps.mp4 -f s16le -
fate-mpegh3da-decode-2-13: REF = $(SAMPLES)/mpegh3da/reference_stereo_to_22.2.pcm

FATE_MPEGH3DA += fate-mpegh3da-decode-2-1
fate-mpegh3da-decode-2-1: CMD = ffmpeg -auto_conversion_filters -c:a libmpeghdec -ch_layout mono -i $(TARGET_SAMPLES)/mpegh3da/mpegh_cicp_02_baseline_32kbps.mp4 -f s16le -
fate-mpegh3da-decode-2-1: REF = $(SAMPLES)/mpegh3da/reference_stereo_to_mono.pcm

FATE_MPEGH3DA += fate-mpegh3da-decode-13-2
fate-mpegh3da-decode-13-2: CMD = ffmpeg -auto_conversion_filters -c:a libmpeghdec -ch_layout stereo -i $(TARGET_SAMPLES)/mpegh3da/mpegh_cicp_13_lc_baseline_compatible_32kbps.mp4 -f s16le -
fate-mpegh3da-decode-13-2: REF = $(SAMPLES)/mpegh3da/reference_22.2_to_stereo.pcm

FATE_MPEGH3DA += fate-mpegh3da-decode-13-13
fate-mpegh3da-decode-13-13: CMD = ffmpeg -auto_conversion_filters -c:a libmpeghdec -ch_layout 22.2 -i $(TARGET_SAMPLES)/mpegh3da/mpegh_cicp_13_lc_baseline_compatible_32kbps.mp4 -f s16le -
fate-mpegh3da-decode-13-13: REF = $(SAMPLES)/mpegh3da/reference_22.2_to_22.2.pcm

FATE_MPEGH3DA += fate-mpegh3da-decode-13-1
fate-mpegh3da-decode-13-1: CMD = ffmpeg -auto_conversion_filters -c:a libmpeghdec -ch_layout mono -i $(TARGET_SAMPLES)/mpegh3da/mpegh_cicp_13_lc_baseline_compatible_32kbps.mp4 -f s16le -
fate-mpegh3da-decode-13-1: REF = $(SAMPLES)/mpegh3da/reference_22.2_to_mono.pcm

FATE_MPEGH3DA += fate-mpegh3da-decode-change-2
fate-mpegh3da-decode-change-2: CMD = ffmpeg -auto_conversion_filters -c:a libmpeghdec -ch_layout stereo -i $(TARGET_SAMPLES)/mpegh3da/mpegh_config_change_cicp_2_14_6_lc_baseline_compatible_32kbps.mp4 -f s16le -
fate-mpegh3da-decode-change-2: REF = $(SAMPLES)/mpegh3da/reference_config_change_to_stereo.pcm

FATE_MPEGH3DA += fate-mpegh3da-decode-change-13
fate-mpegh3da-decode-change-13: CMD = ffmpeg -auto_conversion_filters -c:a libmpeghdec -ch_layout 22.2 -i $(TARGET_SAMPLES)/mpegh3da/mpegh_config_change_cicp_2_14_6_lc_baseline_compatible_32kbps.mp4 -f s16le -
fate-mpegh3da-decode-change-13: REF = $(SAMPLES)/mpegh3da/reference_config_change_to_22.2.pcm

FATE_MPEGH3DA += fate-mpegh3da-decode-change-1
fate-mpegh3da-decode-change-1: CMD = ffmpeg -auto_conversion_filters -c:a libmpeghdec -ch_layout mono -i $(TARGET_SAMPLES)/mpegh3da/mpegh_config_change_cicp_2_14_6_lc_baseline_compatible_32kbps.mp4 -f s16le -
fate-mpegh3da-decode-change-1: REF = $(SAMPLES)/mpegh3da/reference_config_change_to_mono.pcm

$(FATE_MPEGH3DA): CMP = oneoff
$(FATE_MPEGH3DA): CMP_UNIT = s16

FATE_MPEGH3DA-$(call DEMDEC, MOV, LIBMPEGHDEC) += $(FATE_MPEGH3DA)
FATE_SAMPLES_FFMPEG += $(FATE_MPEGH3DA-yes)
fate-mpegh3da: $(FATE_MPEGH3DA-yes)
