package com.lolren.sensevoiceimu

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.TimeUnit

class AudioEngine {
    private val frames = ArrayBlockingQueue<ShortArray>(64)
    private val silence = ShortArray(StreamProtocol.AUDIO_FRAME_SAMPLES)
    @Volatile private var running = true
    @Volatile private var muted = false
    private var previousSequence: Int? = null

    private val worker = Thread({ playbackLoop() }, "sense-audio-playback").apply {
        isDaemon = true
        start()
    }

    @Synchronized
    fun offer(frame: StreamProtocol.AudioFrame) {
        if (muted) return
        if (frame.discontinuity) {
            frames.clear()
            previousSequence = null
        }
        previousSequence?.let { previous ->
            val missing = (frame.sequence - previous - 1) and 0xffff
            if (missing in 1..10) {
                repeat(missing) { frames.offer(silence.copyOf()) }
            } else if (missing > 10) {
                frames.clear()
            }
        }
        if (!frames.offer(frame.samples)) {
            frames.poll()
            frames.offer(frame.samples)
        }
        previousSequence = frame.sequence
    }

    @Synchronized
    fun setMuted(value: Boolean) {
        muted = value
        if (value) {
            frames.clear()
            previousSequence = null
        }
    }

    fun close() {
        running = false
        worker.interrupt()
    }

    private fun playbackLoop() {
        val minimum = AudioTrack.getMinBufferSize(
            StreamProtocol.AUDIO_SAMPLE_RATE,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        val track = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build(),
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(StreamProtocol.AUDIO_SAMPLE_RATE)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .build(),
            )
            .setTransferMode(AudioTrack.MODE_STREAM)
            .setBufferSizeInBytes(maxOf(minimum * 2, 8192))
            .build()
        try {
            while (running && frames.size < 4) Thread.sleep(5)
            if (!running) return
            track.play()
            while (running) {
                val frame = frames.poll(40, TimeUnit.MILLISECONDS) ?: silence
                if (!muted) {
                    track.write(frame, 0, frame.size, AudioTrack.WRITE_BLOCKING)
                }
            }
        } catch (_: InterruptedException) {
            // close() interrupts the bounded wait.
        } finally {
            track.pause()
            track.flush()
            track.release()
        }
    }
}
