tests/data/live_no_endlist.m3u8: TAG = GEN
tests/data/live_no_endlist.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -v verbose -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_time 3 -map 0 \
        -hls_flags omit_endlist -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/live_no_endlist_%03d.ts \
        $(TARGET_PATH)/tests/data/live_no_endlist.m3u8 2>/dev/null

FATE_HLSENC-$(call FILTERDEMDECENCMUX, HDCD AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED PCM_S24LE, HLS MPEGTS PCM_S24LE, LAVFI_INDEV) += fate-hls-live-no-endlist
fate-hls-live-no-endlist: tests/data/live_no_endlist.m3u8
fate-hls-live-no-endlist: SRC = $(TARGET_PATH)/tests/data/live_no_endlist.m3u8
fate-hls-live-no-endlist: CMD = md5 -i $(SRC) -af hdcd=process_stereo=false -t 6 -f s24le
fate-hls-live-no-endlist: CMP = oneline
fate-hls-live-no-endlist: REF = e038bb8e65d4c1745b9b3ed643e607a3

tests/data/live_last_endlist.m3u8: TAG = GEN
tests/data/live_last_endlist.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -v verbose -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_time 3 -map 0 \
        -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/live_last_endlist_%03d.ts \
        $(TARGET_PATH)/tests/data/live_last_endlist.m3u8 2>/dev/null

FATE_HLSENC-$(call FILTERDEMDECENCMUX, HDCD AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED PCM_S24LE, HLS MPEGTS PCM_S24LE, LAVFI_INDEV) += fate-hls-live-last-endlist
fate-hls-live-last-endlist: tests/data/live_last_endlist.m3u8
fate-hls-live-last-endlist: SRC = $(TARGET_PATH)/tests/data/live_last_endlist.m3u8
fate-hls-live-last-endlist: CMD = md5 -i $(SRC) -af hdcd=process_stereo=false -t 6 -f s24le
fate-hls-live-last-endlist: CMP = oneline
fate-hls-live-last-endlist: REF = 2ca8567092dcf01e37bedd50454d1ab7


tests/data/live_endlist.m3u8: TAG = GEN
tests/data/live_endlist.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_time 3 -map 0 \
        -hls_list_size 0 -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/live_endlist_%d.ts \
        $(TARGET_PATH)/tests/data/live_endlist.m3u8 2>/dev/null

FATE_HLSENC-$(call FILTERDEMDECENCMUX, HDCD AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED PCM_S24LE, HLS MPEGTS PCM_S24LE, LAVFI_INDEV ) += fate-hls-live-endlist
fate-hls-live-endlist: tests/data/live_endlist.m3u8
fate-hls-live-endlist: SRC = $(TARGET_PATH)/tests/data/live_endlist.m3u8
fate-hls-live-endlist: CMD = md5 -i $(SRC) -af hdcd=process_stereo=false -t 20 -f s24le
fate-hls-live-endlist: CMP = oneline
fate-hls-live-endlist: REF = e189ce781d9c87882f58e3929455167b

tests/data/hls_segment_size.m3u8: TAG = GEN
tests/data/hls_segment_size.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_segment_size 300000 -map 0 \
	-hls_list_size 0 -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/hls_segment_size_%d.ts \
	$(TARGET_PATH)/tests/data/hls_segment_size.m3u8 2>/dev/null

FATE_HLSENC-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-segment-size
fate-hls-segment-size: tests/data/hls_segment_size.m3u8
fate-hls-segment-size: CMD = framecrc -auto_conversion_filters -flags +bitexact -i $(TARGET_PATH)/tests/data/hls_segment_size.m3u8 -vf setpts=N*23

tests/data/hls_segment_single.m3u8: TAG = GEN
tests/data/hls_segment_single.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_flags single_file -map 0 \
	-hls_list_size 0 -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/hls_segment_single.ts \
	$(TARGET_PATH)/tests/data/hls_segment_single.m3u8 2>/dev/null

FATE_HLSENC-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-segment-single
fate-hls-segment-single: tests/data/hls_segment_single.m3u8
fate-hls-segment-single: CMD = framecrc -auto_conversion_filters -flags +bitexact -i $(TARGET_PATH)/tests/data/hls_segment_single.m3u8 -vf setpts=N*23

tests/data/hls_init_time.m3u8: TAG = GEN
tests/data/hls_init_time.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=11" -f hls -hls_init_time 1 -hls_time 3 -map 0 \
	-hls_list_size 5 -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/hls_init_time_%d.ts \
	$(TARGET_PATH)/tests/data/hls_init_time.m3u8 2>/dev/null

FATE_HLSENC-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-init-time
fate-hls-init-time: tests/data/hls_init_time.m3u8
fate-hls-init-time: CMD = framecrc -auto_conversion_filters -flags +bitexact -i $(TARGET_PATH)/tests/data/hls_init_time.m3u8 -vf setpts=N*23

tests/data/hls_list_size.m3u8: TAG = GEN
tests/data/hls_list_size.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_time 4 -map 0 \
	-hls_list_size 4 -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/hls_list_size_%d.ts \
	$(TARGET_PATH)/tests/data/hls_list_size.m3u8 2>/dev/null

FATE_HLSENC-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-list-size
fate-hls-list-size: tests/data/hls_list_size.m3u8
fate-hls-list-size: CMD = framecrc -auto_conversion_filters -flags +bitexact -i $(TARGET_PATH)/tests/data/hls_list_size.m3u8 -vf setpts=N*23

tests/data/hls_fmp4.m3u8: TAG = GEN
tests/data/hls_fmp4.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -re -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=5" -map 0 -codec:a mp2fixed \
	-hls_segment_type mpegts -hls_fmp4_init_filename now.mp4 -hls_list_size 0 \
	-hls_time 1 -hls_segment_filename "$(TARGET_PATH)/tests/data/hls_fmp4_%d.m4s" \
	$(TARGET_PATH)/tests/data/hls_fmp4.m3u8 2>/dev/null

FATE_HLSENC-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-fmp4
fate-hls-fmp4: tests/data/hls_fmp4.m3u8
fate-hls-fmp4: CMD = framecrc -auto_conversion_filters -flags +bitexact -i $(TARGET_PATH)/tests/data/hls_fmp4.m3u8 -vf setpts=N*23

tests/data/hls_fmp4_ac3.m3u8: TAG = GEN
tests/data/hls_fmp4_ac3.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-stream_loop 4 -i $(TARGET_SAMPLES)/ac3/monsters_inc_5.1_448_small.ac3 -c copy -map 0 \
	-hls_segment_type fmp4 -hls_fmp4_init_filename now_ac3.mp4 -hls_list_size 0 \
	-hls_time 2 -hls_segment_filename "$(TARGET_PATH)/tests/data/hls_fmp4_ac3_%d.m4s" \
	$(TARGET_PATH)/tests/data/hls_fmp4_ac3.m3u8 2>/dev/null

FATE_HLSENC-yes := $(if $(call FRAMECRC), $(FATE_HLSENC-yes))

FATE_HLSENC_PROBE-$(call DEMMUX, HLS AC3, HLS MP4, AC3_DECODER) += fate-hls-fmp4_ac3
fate-hls-fmp4_ac3: tests/data/hls_fmp4_ac3.m3u8
fate-hls-fmp4_ac3: CMD = probeaudiostream $(TARGET_PATH)/tests/data/now_ac3.mp4


tests/data/hls_cmfa.m3u8: TAG = GEN
tests/data/hls_cmfa.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-i $(TARGET_SAMPLES)/aac/al06_44.mp4 -c copy -map 0 \
	-hls_segment_type fmp4 -hls_fmp4_init_filename now.cmfa -hls_list_size 0 \
	-hls_time 1 -hls_segment_filename "$(TARGET_PATH)/tests/data/hls_fmp4_%d.cmfa" \
	-t 1 $(TARGET_PATH)/tests/data/hls_cmfa.m3u8 2>/dev/null

FATE_HLSENC-yes := $(if $(call FRAMECRC), $(FATE_HLSENC-yes))

FATE_HLSENC_PROBE-$(call FRAMECRC, HLS) += fate-hls-cmfa
fate-hls-cmfa: tests/data/hls_cmfa.m3u8
fate-hls-cmfa: CMD = framecrc -i $(TARGET_PATH)/tests/data/hls_cmfa.m3u8 -c copy

FATE_SAMPLES_FFMPEG += $(FATE_HLSENC-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_HLSENC_PROBE-yes)
fate-hlsenc: $(FATE_HLSENC-yes) $(FATE_HLSENC_PROBE-yes)

# ---------------------------------------------------------------------------
# Additional tests for improved code coverage
# ---------------------------------------------------------------------------

# round_durations flag: exercises hls_window() HLS version 2 path and
# rounded EXTINF writing via ff_hls_write_file_entry(round_durations=1).
tests/data/hls_round_durations.m3u8: TAG = GEN
tests/data/hls_round_durations.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=10" -f hls \
	-hls_time 3 -hls_list_size 0 -hls_flags round_durations \
	-codec:a mp2fixed \
	-hls_segment_filename "$(TARGET_PATH)/tests/data/hls_round_durations_%d.ts" \
	$(TARGET_PATH)/tests/data/hls_round_durations.m3u8 2>/dev/null

FATE_HLSENC_EXTRA-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-round-durations
fate-hls-round-durations: tests/data/hls_round_durations.m3u8
fate-hls-round-durations: CMD = framecrc -auto_conversion_filters -flags +bitexact \
	-i $(TARGET_PATH)/tests/data/hls_round_durations.m3u8 -vf setpts=N*23

# split_by_time flag: exercises can_split path triggered by wall-clock time
# rather than keyframe boundary in hls_write_packet().
tests/data/hls_split_by_time.m3u8: TAG = GEN
tests/data/hls_split_by_time.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=10" -f hls \
	-hls_time 3 -hls_list_size 0 -hls_flags split_by_time \
	-codec:a mp2fixed \
	-hls_segment_filename "$(TARGET_PATH)/tests/data/hls_split_by_time_%d.ts" \
	$(TARGET_PATH)/tests/data/hls_split_by_time.m3u8 2>/dev/null

FATE_HLSENC_EXTRA-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-split-by-time
fate-hls-split-by-time: tests/data/hls_split_by_time.m3u8
fate-hls-split-by-time: CMD = framecrc -auto_conversion_filters -flags +bitexact \
	-i $(TARGET_PATH)/tests/data/hls_split_by_time.m3u8 -vf setpts=N*23

# discont_start flag: exercises the opening #EXT-X-DISCONTINUITY tag in
# hls_window() and the HLS_DISCONT_START branch.
tests/data/hls_discont_start.m3u8: TAG = GEN
tests/data/hls_discont_start.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=10" -f hls \
	-hls_time 4 -hls_list_size 0 -hls_flags discont_start \
	-codec:a mp2fixed \
	-hls_segment_filename "$(TARGET_PATH)/tests/data/hls_discont_start_%d.ts" \
	$(TARGET_PATH)/tests/data/hls_discont_start.m3u8 2>/dev/null

FATE_HLSENC_EXTRA-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-discont-start
fate-hls-discont-start: tests/data/hls_discont_start.m3u8
fate-hls-discont-start: CMD = framecrc -auto_conversion_filters -flags +bitexact \
	-i $(TARGET_PATH)/tests/data/hls_discont_start.m3u8 -vf setpts=N*23

# independent_segments flag: exercises the #EXT-X-INDEPENDENT-SEGMENTS line in
# hls_window(), pushes playlist to version 6.
tests/data/hls_independent_segs.m3u8: TAG = GEN
tests/data/hls_independent_segs.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=10" -f hls \
	-hls_time 3 -hls_list_size 0 -hls_flags independent_segments \
	-codec:a mp2fixed \
	-hls_segment_filename "$(TARGET_PATH)/tests/data/hls_independent_segs_%d.ts" \
	$(TARGET_PATH)/tests/data/hls_independent_segs.m3u8 2>/dev/null

FATE_HLSENC_EXTRA-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-independent-segs
fate-hls-independent-segs: tests/data/hls_independent_segs.m3u8
fate-hls-independent-segs: CMD = framecrc -auto_conversion_filters -flags +bitexact \
	-i $(TARGET_PATH)/tests/data/hls_independent_segs.m3u8 -vf setpts=N*23

# EVENT playlist type: exercises hls->pl_type == PLAYLIST_TYPE_EVENT branch,
# disables sliding window (max_nb_segments=0) inside hls_append_segment().
tests/data/hls_event_playlist.m3u8: TAG = GEN
tests/data/hls_event_playlist.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=10" -f hls \
	-hls_time 3 -hls_list_size 5 -hls_playlist_type event \
	-codec:a mp2fixed \
	-hls_segment_filename "$(TARGET_PATH)/tests/data/hls_event_playlist_%d.ts" \
	$(TARGET_PATH)/tests/data/hls_event_playlist.m3u8 2>/dev/null

FATE_HLSENC_EXTRA-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-event-playlist
fate-hls-event-playlist: tests/data/hls_event_playlist.m3u8
fate-hls-event-playlist: CMD = framecrc -auto_conversion_filters -flags +bitexact \
	-i $(TARGET_PATH)/tests/data/hls_event_playlist.m3u8 -vf setpts=N*23

# VOD playlist type: exercises hls->pl_type == PLAYLIST_TYPE_VOD, writes
# #EXT-X-PLAYLIST-TYPE:VOD once only at the very end.
tests/data/hls_vod.m3u8: TAG = GEN
tests/data/hls_vod.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=10" -f hls \
	-hls_time 3 -hls_list_size 0 -hls_playlist_type vod \
	-codec:a mp2fixed \
	-hls_segment_filename "$(TARGET_PATH)/tests/data/hls_vod_%d.ts" \
	$(TARGET_PATH)/tests/data/hls_vod.m3u8 2>/dev/null

FATE_HLSENC_EXTRA-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-vod
fate-hls-vod: tests/data/hls_vod.m3u8
fate-hls-vod: CMD = framecrc -auto_conversion_filters -flags +bitexact \
	-i $(TARGET_PATH)/tests/data/hls_vod.m3u8 -vf setpts=N*23

# delete_segments flag: exercises hls_delete_old_segments() which is called
# from hls_append_segment() every time hls_list_size is exceeded.
tests/data/hls_delete_segs.m3u8: TAG = GEN
tests/data/hls_delete_segs.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls \
	-hls_time 3 -hls_list_size 3 -hls_flags delete_segments \
	-codec:a mp2fixed \
	-hls_segment_filename "$(TARGET_PATH)/tests/data/hls_delete_segs_%d.ts" \
	$(TARGET_PATH)/tests/data/hls_delete_segs.m3u8 2>/dev/null

FATE_HLSENC_EXTRA-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-delete-segs
fate-hls-delete-segs: tests/data/hls_delete_segs.m3u8
fate-hls-delete-segs: CMD = framecrc -auto_conversion_filters -flags +bitexact \
	-i $(TARGET_PATH)/tests/data/hls_delete_segs.m3u8 -vf setpts=N*23

# start_number option: exercises the hls->start_sequence path in hls_init()
# and the sequence initialisation in each VariantStream.
tests/data/hls_start_number.m3u8: TAG = GEN
tests/data/hls_start_number.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=10" -f hls \
	-hls_time 3 -hls_list_size 0 -start_number 5 \
	-codec:a mp2fixed \
	-hls_segment_filename "$(TARGET_PATH)/tests/data/hls_start_number_%d.ts" \
	$(TARGET_PATH)/tests/data/hls_start_number.m3u8 2>/dev/null

FATE_HLSENC_EXTRA-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-start-number
fate-hls-start-number: tests/data/hls_start_number.m3u8
fate-hls-start-number: CMD = framecrc -auto_conversion_filters -flags +bitexact \
	-i $(TARGET_PATH)/tests/data/hls_start_number.m3u8 -vf setpts=N*23

# temp_file flag: exercises use_temp_file path in hls_start() / hls_write_trailer(),
# writing to a .tmp file first and then renaming via hls_rename_temp_file().
tests/data/hls_temp_file.m3u8: TAG = GEN
tests/data/hls_temp_file.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=10" -f hls \
	-hls_time 4 -hls_list_size 0 -hls_flags temp_file \
	-codec:a mp2fixed \
	-hls_segment_filename "$(TARGET_PATH)/tests/data/hls_temp_file_%d.ts" \
	$(TARGET_PATH)/tests/data/hls_temp_file.m3u8 2>/dev/null

FATE_HLSENC_EXTRA-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-temp-file
fate-hls-temp-file: tests/data/hls_temp_file.m3u8
fate-hls-temp-file: CMD = framecrc -auto_conversion_filters -flags +bitexact \
	-i $(TARGET_PATH)/tests/data/hls_temp_file.m3u8 -vf setpts=N*23

# baseurl option: exercises hls->baseurl prepending in hls_window() via
# ff_hls_write_file_entry(hls->baseurl, ...).
tests/data/hls_baseurl.m3u8: TAG = GEN
tests/data/hls_baseurl.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=9" -f hls \
	-hls_time 3 -hls_list_size 0 \
	-hls_base_url "http://example.com/segments/" \
	-codec:a mp2fixed \
	-hls_segment_filename "$(TARGET_PATH)/tests/data/hls_baseurl_%d.ts" \
	$(TARGET_PATH)/tests/data/hls_baseurl.m3u8 2>/dev/null

FATE_HLSENC_EXTRA-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-baseurl
fate-hls-baseurl: tests/data/hls_baseurl.m3u8
fate-hls-baseurl: CMD = framecrc -auto_conversion_filters -flags +bitexact \
	-i $(TARGET_PATH)/tests/data/hls_baseurl.m3u8 -vf setpts=N*23

# allowcache=0: exercises hls->allowcache == 0 branch where
# ff_hls_write_playlist_header writes #EXT-X-ALLOW-CACHE:NO.
tests/data/hls_allow_cache.m3u8: TAG = GEN
tests/data/hls_allow_cache.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=9" -f hls \
	-hls_time 3 -hls_list_size 0 -hls_allow_cache 0 \
	-codec:a mp2fixed \
	-hls_segment_filename "$(TARGET_PATH)/tests/data/hls_allow_cache_%d.ts" \
	$(TARGET_PATH)/tests/data/hls_allow_cache.m3u8 2>/dev/null

FATE_HLSENC_EXTRA-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV) += fate-hls-allow-cache
fate-hls-allow-cache: tests/data/hls_allow_cache.m3u8
fate-hls-allow-cache: CMD = framecrc -auto_conversion_filters -flags +bitexact \
	-i $(TARGET_PATH)/tests/data/hls_allow_cache.m3u8 -vf setpts=N*23

FATE_SAMPLES_FFMPEG += $(FATE_HLSENC_EXTRA-yes)
fate-hlsenc: $(FATE_HLSENC_EXTRA-yes)
