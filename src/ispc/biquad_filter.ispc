#define SQRT_2					( 1.41421356237309504880 )

export struct BiquadBuffer
{
	uniform float in_[2];
	uniform float out_[2];
};

export struct BiquadCoeff
{
	uniform float in_[3];
	uniform float out_[2];
};

export uniform BiquadBuffer biquad_buffer()
{
	uniform BiquadBuffer buffer;

	buffer.in_[0] = 0.0;
	buffer.in_[1] = 0.0;
	buffer.out_[0] = 0.0;
	buffer.out_[1] = 0.0;

	return buffer;
}

export uniform BiquadCoeff biquad_filter_passthrough()
{
	uniform BiquadCoeff coeff;

	coeff.in_[0] = 1.0f;
	coeff.in_[1] = 0.0f;
	coeff.in_[2] = 0.0f;
	coeff.out_[0] = 0.0f;
	coeff.out_[1] = 0.0f;

	return coeff;
}

export uniform BiquadCoeff biquad_filter_lowpass(uniform float rate, uniform float cutoff, uniform float resonance)
{
	uniform BiquadCoeff coeff;

	uniform float initcoef = 1.0 / tan(PI * cutoff / rate);
	uniform float initcoefpw2 = initcoef * initcoef;
	uniform float resrange = ((SQRT_2 - 0.01) * (1.0 - resonance)) + 0.01;

	coeff.in_[0] = 1.0 / (1.0 + (resrange * initcoef) + initcoefpw2);
	coeff.in_[1] = 2.0 * coeff.in_[0];
	coeff.in_[2] = coeff.in_[0];

	// Negated to make processing a series of madds.
	coeff.out_[0] = -2.0 * coeff.in_[0] * (1.0 - initcoefpw2);
	coeff.out_[1] = -coeff.in_[0] * (1.0 - resrange * initcoef + initcoefpw2);

	return coeff;
}

export uniform BiquadCoeff biquad_filter_highpass(uniform float rate, uniform float cutoff, uniform float resonance)
{
	uniform BiquadCoeff coeff;

	uniform float initcoef = tan(PI * cutoff / rate);
	uniform float initcoefpw2 = initcoef * initcoef;
	uniform float resrange = ((SQRT_2 - 0.01) * (1.0 - resonance)) + 0.01;

	coeff.in_[0] = 1.0 / (1.0 + (resrange * initcoef) + initcoefpw2);
	coeff.in_[1] = -2.0 * coeff.in_[0];
	coeff.in_[2] = coeff.in_[0];

	// Negated to make processing a series of madds.
	coeff.out_[0] = -2.0 * coeff.in_[0] * (initcoefpw2 - 1.0);
	coeff.out_[1] = -coeff.in_[0] * (1.0 - resrange * initcoef + initcoefpw2);

	return coeff;
}

export void biquad_filter_process(uniform const BiquadCoeff coeff[], uniform BiquadBuffer buffer[], uniform const float invalues[], uniform float outvalues[], uniform int numsamples)
{
	uniform float<4> buffer_swizzle = 
	{
		buffer[0].in_[0], buffer[0].in_[1],
		buffer[0].out_[0], buffer[0].out_[1]
	};
	for(uniform int i = 0; i < numsamples; ++i)
	{
		uniform float value = invalues[i];
		uniform float newvalue = coeff[0].in_[0] * value +
					coeff[0].in_[1] * buffer_swizzle.x +
					coeff[0].in_[2] * buffer_swizzle.y +
					coeff[0].out_[0] * buffer_swizzle.z +
					coeff[0].out_[1] * buffer_swizzle.w;
	
		buffer_swizzle.y = buffer_swizzle.x;
		buffer_swizzle.x = value;
				
		buffer_swizzle.w = buffer_swizzle.z;
		buffer_swizzle.z = newvalue;

		outvalues[i] = buffer_swizzle.z;
	}

	buffer[0].in_[0] = buffer_swizzle.x;
	buffer[0].in_[1] = buffer_swizzle.y;
	buffer[0].out_[0] = buffer_swizzle.z;
	buffer[0].out_[1] = buffer_swizzle.w;
}

