export void acf_process(uniform int numvalues, uniform const float invalues[], uniform float outvalues[])
{
	// initial naive impl.
	for(uniform int i = 0; i < numvalues; ++i)
	{
		uniform float sum = 0.0;
		for(uniform int j = 0; j < numvalues - i; ++j)
		{
			sum += invalues[i] * invalues[j + i];
		}
		outvalues[i] = sum;
	}
}

export uniform int acf_largest_peak_period(uniform int numvalues, uniform const float invalues[])
{
	uniform int i = 0;
	uniform float prev = 0.0;
	uniform float curr = 0.0;
	uniform float prevgrad = 0.0;
	uniform float currgrad = 0.0;

	uniform float max0 = 0.0;
	uniform int max0idx = -1;
	uniform float max1 = 0.0;
	uniform int max1idx = -1;

	prev = 0.0;
	prevgrad = 0.0;
	for(i = 0; i < numvalues; ++i)
	{
		curr = invalues[i];
		currgrad = prev - curr;

		if((currgrad > 0.0 && prevgrad < 0.0) ||
		   (currgrad < 0.0 && prevgrad > 0.0))
		{
			if(curr > max0)
			{
				max0 = curr;
				max0idx = i;
			}
			else if(curr > max1 && curr <= max0)
			{
				max1 = curr;
				max1idx = i;
			}
		}

		if(currgrad != 0.0)
		{
			prevgrad = currgrad;
		}
		prev = curr;
	}

	return max1idx - max0idx;
}