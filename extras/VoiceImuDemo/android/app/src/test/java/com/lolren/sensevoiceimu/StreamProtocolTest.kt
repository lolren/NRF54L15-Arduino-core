package com.lolren.sensevoiceimu

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test

class StreamProtocolTest {
    @Test
    fun controlPacketIsLittleEndian() {
        assertEquals(
            listOf(1, 3, 0x34, 0x12),
            StreamProtocol.control(3, 0x1234).map { it.toInt() and 0xff },
        )
    }

    @Test
    fun independentAdpcmFrameDecodes() {
        val packet = ByteArray(StreamProtocol.AUDIO_PACKET_BYTES)
        val buffer = ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN)
        packet[0] = StreamProtocol.VERSION.toByte()
        packet[1] = 1
        buffer.putShort(2, 42)
        buffer.putInt(4, 3200)
        buffer.putShort(8, StreamProtocol.AUDIO_FRAME_SAMPLES.toShort())
        buffer.putShort(10, 1000)
        packet[12] = 0
        val frame = StreamProtocol.decodeAudio(packet)
        assertNotNull(frame)
        frame!!
        assertEquals(42, frame.sequence)
        assertEquals(3200L, frame.sampleCounter)
        assertEquals(true, frame.discontinuity)
        assertEquals(StreamProtocol.AUDIO_FRAME_SAMPLES, frame.samples.size)
        assertEquals(1000, frame.samples[0].toInt())
    }

    @Test
    fun imuRawValuesUseDocumentedScales() {
        val packet = ByteArray(StreamProtocol.IMU_PACKET_BYTES)
        val buffer = ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN)
        packet[0] = StreamProtocol.VERSION.toByte()
        buffer.putShort(2, 7)
        buffer.putInt(4, 640)
        buffer.putShort(8, 16384)
        buffer.putShort(10, (-16384).toShort())
        buffer.putShort(12, 8192)
        buffer.putShort(14, 1000)
        buffer.putShort(16, (-1000).toShort())
        buffer.putShort(18, 500)
        val frame = StreamProtocol.decodeImu(packet)
        assertNotNull(frame)
        frame!!
        assertEquals(7, frame.sequence)
        assertEquals(0.999424f, frame.ax, 0.000001f)
        assertEquals(-0.999424f, frame.ay, 0.000001f)
        assertEquals(8.75f, frame.gx, 0.000001f)
        assertEquals(-8.75f, frame.gy, 0.000001f)
    }
}
