package cp1.solution;

import cp1.base.*;

public class ResourcePair
{
    private int index;
    private ResourceOperation resourceOper;

    public ResourcePair(int index, ResourceOperation resourceOper)
    {
        this.index = index;
        this.resourceOper = resourceOper;
    }

    public int getIndex()
    {
        return index;
    }

    public ResourceOperation getResourceOper()
    {
        return resourceOper;
    }
}
