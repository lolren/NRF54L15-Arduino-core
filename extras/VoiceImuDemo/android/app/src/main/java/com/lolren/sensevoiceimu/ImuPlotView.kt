package com.lolren.sensevoiceimu

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.util.TypedValue
import android.view.View
import java.util.Locale

class ImuPlotView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {
    private val capacity = 150
    private val accel = Array(3) { FloatArray(capacity) }
    private val gyro = Array(3) { FloatArray(capacity) }
    private var count = 0
    private var cursor = 0
    private val linePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2.2f * resources.displayMetrics.density
    }
    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(220, 226, 222)
        strokeWidth = resources.displayMetrics.density
    }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(69, 82, 77)
        textSize = TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_SP,
            12f,
            resources.displayMetrics,
        )
    }
    private val colors = intArrayOf(
        Color.rgb(0, 126, 105),
        Color.rgb(26, 104, 168),
        Color.rgb(196, 142, 19),
        Color.rgb(201, 63, 43),
        Color.rgb(139, 74, 157),
        Color.rgb(80, 88, 91),
    )

    @Synchronized
    fun add(frame: StreamProtocol.ImuFrame) {
        accel[0][cursor] = frame.ax
        accel[1][cursor] = frame.ay
        accel[2][cursor] = frame.az
        gyro[0][cursor] = frame.gx
        gyro[1][cursor] = frame.gy
        gyro[2][cursor] = frame.gz
        cursor = (cursor + 1) % capacity
        count = minOf(count + 1, capacity)
        postInvalidateOnAnimation()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawColor(Color.rgb(250, 251, 249))
        val density = resources.displayMetrics.density
        val left = 42f * density
        val right = width - 12f * density
        val top = 26f * density
        val middle = height / 2f
        val bottom = height - 20f * density
        drawGrid(canvas, left, right, top, middle - 12f * density, "ACCEL  +/-2 g")
        drawGrid(canvas, left, right, middle + 18f * density, bottom, "GYRO  +/-250 deg/s")
        drawSeries(canvas, accel, left, right, top, middle - 12f * density, 2f, 0)
        drawSeries(canvas, gyro, left, right, middle + 18f * density, bottom, 250f, 3)
        drawLegend(canvas, right)
    }

    private fun drawGrid(
        canvas: Canvas,
        left: Float,
        right: Float,
        top: Float,
        bottom: Float,
        label: String,
    ) {
        canvas.drawText(label, left, top - 8f * resources.displayMetrics.density, labelPaint)
        for (i in 0..4) {
            val y = top + (bottom - top) * i / 4f
            canvas.drawLine(left, y, right, y, gridPaint)
        }
        canvas.drawLine(left, top, left, bottom, gridPaint)
    }

    @Synchronized
    private fun drawSeries(
        canvas: Canvas,
        values: Array<FloatArray>,
        left: Float,
        right: Float,
        top: Float,
        bottom: Float,
        scale: Float,
        colorOffset: Int,
    ) {
        if (count < 2) return
        val center = (top + bottom) / 2f
        val halfHeight = (bottom - top) / 2f
        for (axis in 0..2) {
            linePaint.color = colors[colorOffset + axis]
            var previousX = left
            var previousY = center
            for (point in 0 until count) {
                val index = (cursor - count + point + capacity) % capacity
                val x = left + (right - left) * point / (capacity - 1f)
                val normalized = (values[axis][index] / scale).coerceIn(-1f, 1f)
                val y = center - normalized * halfHeight
                if (point > 0) canvas.drawLine(previousX, previousY, x, y, linePaint)
                previousX = x
                previousY = y
            }
        }
    }

    private fun drawLegend(canvas: Canvas, right: Float) {
        val labels = arrayOf("Ax", "Ay", "Az", "Gx", "Gy", "Gz")
        var x = right - 188f * resources.displayMetrics.density
        val y = 17f * resources.displayMetrics.density
        labels.forEachIndexed { index, label ->
            labelPaint.color = colors[index]
            canvas.drawText(label, x, y, labelPaint)
            x += 31f * resources.displayMetrics.density
        }
        labelPaint.color = Color.rgb(69, 82, 77)
    }
}
