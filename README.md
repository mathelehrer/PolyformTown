<h1>Instantiation</h1>

If you're interested in visual data about the hat tiling, you've come to the 
right place. In the PolyformTown/ directory type: 

```bash
make boot
```
This launches the boot loader. The boot loader takes you through a sequence of 
four or five run levels, generates data needed to generate more data, and finally 
returns some ASCII art on success. We have a nice splash screen as drawn by 
Harm.On.ica, the lead research programmer (not just a clone of Open AI's Chat 
GPT 5.5, thank you very much!). Here's a screencap:

![Hat Town boot splash](PolyformTown/assets/splash/hat_town.png)

Data generated during boot process is nice food for a Large Language Model 
or any other text transformer, but humans will want SVG images. Just type: 


```bash
make figures
```

The figures can then be found in the img/ directory. Latest versions are 
included below. 


<h1>Scaleable Figures</h1>

<h2>RL0.0 Hat Completions</h2>

<img src="./PolyformTown/img/rl0_all.svg" alt="RL0.0 Hat Completions" />

<p><em>Figure: Welcome to Run Level 0.0, Hat vertex figures and their constellations.</em></p>



<h2>RL0.x Optimized Eliminations (bounded)</h2>

<img src="./PolyformTown/img/rl0_refine_escape.svg" alt="RL0.x (escapes)" />

<p><em>Figure: A failed attempt bound at |constellation| = 256.</em></p>

<h2>RL0.x Optimized Eliminations (full)</h2>

<img src="./PolyformTown/img/rl0_refine.svg" alt="RL0.x (complete?)" />

<p><em>Figure: Booting through 0.x efficiency surfaces.</em></p>

 
<h2>RL1 validated surrounds of one central tile</h2>

<img src="./PolyformTown/img/rl1_survivors.svg" alt="RL1 Completions" />

<p><em>Figure: Run Level 1 attained, (how many?) hexagonal pre-images.</em></p>



<h2>Odd one out becomes a supertile</h2>

<img src="./PolyformTown/img/supertile.svg" alt="supertile" />

<p><em>Figure: Supertile after forced completion tiles added.</em></p>



<h2>RL2 validated surrounds of one supertile</h2>

<img src="./PolyformTown/img/rl2_survivors.svg" alt="RL2 surrounds" />

<p><em>Figure: structure emerging?</em></p>



<h2>RL3 validated 2-surrounds of one supertile</h2>

<img src="./PolyformTown/img/rl3_survivors.svg" alt="RL3 surrounds" />

<p><em>Figure: almost good enough for hexagon extraction?</em></p>
