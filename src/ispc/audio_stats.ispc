export uniform float audio_stats_rms(const uniform float data[], uniform int numsamples)
{
	float rms = 0.0;
	foreach(i = 0 ... numsamples)
	{
		rms += data[i] * data[i];
	}

	uniform float outrms = reduce_add(rms);
	outrms = sqrt((1.0 / (float)numsamples) * outrms);
	return outrms;
}

export uniform float audio_stats_max(const uniform float data[], uniform int numsamples)
{
	float mmin = 0.0;
	float mmax = 0.0;
	foreach(i = 0 ... numsamples)
	{
		mmin = min(mmin, data[i]);
		mmax = max(mmax, data[i]);
	}

	uniform float outmin = reduce_min(mmin);
	uniform float outmax = reduce_max(mmax);
	return (outmax - outmin) * 0.5;
}

