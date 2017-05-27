export void clipping_hard(uniform const float invalues[], uniform float outvalues[], uniform int numsamples)
{
	foreach(i = 0 ... numsamples)
	{
		float value = invalues[i];
		if(value > 1.0)
			value = 1.0;
		else if(value < -1.0)
			value = -1.0;
		outvalues[i] = value;
	}
}
