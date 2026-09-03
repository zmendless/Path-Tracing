#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform vec2 u_resolution;
uniform int u_frame;
uniform float u_time;

uniform vec3 u_ro;
uniform vec3 u_forward;
uniform vec3 u_right;
uniform vec3 u_up;
uniform float u_fov;

const mat2 ROT30 = mat2(0.8660254, -0.5, 0.5, 0.8660254);
const mat2 ROT80 = mat2(0.1736482, -0.9848077, 0.9848077, 0.1736482);

const int MAX_BOUNCES = 8;
const int RR_START_BOUNCE = 6;

const float COLOR_BLEED_SATURATION = 1.5;
const float COLOR_BLEED_STRENGTH   = 1.18;

float sdPlane(vec3 p, vec3 n, float h) {
    return dot(p, n) + h;
}

float sdSphere(vec3 p, float r) {
    return length(p) - r;
}

float sdBox( vec3 p, vec3 b ) {
  vec3 q = abs(p) - b;
  return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);
}

vec3 sky(vec3 rd) {
    rd = normalize(rd);

    vec3 sunDir = normalize(vec3(1.0, 0.5, -0.7));

    float t = 0.5 * (rd.y + 1.0);

    vec3 color = mix(vec3(0.75, 0.85, 1.0), vec3(0.08, 0.25, 0.55), t);

    float sun = max(dot(rd, sunDir), 0.0);
    color += vec3(10.0) * pow(sun, 500.0);

    return color;
}

vec2 opU(vec2 a, vec2 b) {
    return a.x < b.x ? a : b;
}

vec2 mapMaterial(vec3 p) {
    vec2 res = vec2(1e10, 0.0);

    vec3 q = p - vec3(-2.5, 0, 2);
    q.xz = ROT30 * q.xz;
    res = opU(res, vec2(sdBox(q, vec3(1., 2.5, 1.)), 1.0));

    q = p - vec3(-2.5, 0, 2);
    q.xz = ROT80 * q.xz;
    res = opU(res, vec2(sdBox(q, vec3(0.5, 3.5, 0.5)), 1.0));

    res = opU(res, vec2(sdBox(p - vec3(0.0, 4.0, -1.0), vec3(1.0, 0.01, 0.5)), 9.0));
    res = opU(res, vec2(sdBox(p - vec3(.0, 1.0, 5.0), vec3(3.0, 3.0, 0.01)), 1.0));

    res = opU(res, vec2(sdSphere(p - vec3(2, -0.1, 2), 2.1), 6.0));
    res = opU(res, vec2(sdSphere(p - vec3(0.5, -1.0, -0.6), 1.0), 0.0));

    res = opU(res, vec2(sdPlane(p - vec3(0, 1, 0), vec3(0, 1, 0), 3.0), 2.0));   // floor
    res = opU(res, vec2(sdPlane(p - vec3(0, 1, 0), vec3(0, 0, -1), 5.0), 7.0)); // back
    res = opU(res, vec2(sdPlane(p - vec3(0, 1, 0), vec3(0, 0, 1), 7.0), 7.0));  // front
    res = opU(res, vec2(sdPlane(p - vec3(0, 1, 0), vec3(1, 0, 0), 5.0), 3.0));   // right
    res = opU(res, vec2(sdPlane(p - vec3(0, 1, 0), vec3(-1, 0, 0), 5.0), 5.0));  // left
    res = opU(res, vec2(sdPlane(p - vec3(0, 1, 0), vec3(0, -1, 0), 5.0), 7.0));  // ceiling

    return res;
}

float map(vec3 p) {
    return mapMaterial(p).x;
}

vec3 getNormal(vec3 p) {
    const float e = 0.001;
    const vec2 h = vec2(1.0, -1.0);
    return normalize(
        h.xyy * map(p + h.xyy * e) +
        h.yyx * map(p + h.yyx * e) +
        h.yxy * map(p + h.yxy * e) +
        h.xxx * map(p + h.xxx * e)
    );
}

float shadow(vec3 ro, vec3 rd, float maxT) {
    float t = 0.01;
    float res = 1.0;

    for (int i = 0; i < 20; i++) {
        float d = map(ro + rd * t);

        if (d < 0.001)
            return 0.0;

        res = min(res, 8.0 * d / t);
        t += d;

        if (t > maxT)
            break;
    }

    return res;
}

float random(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float random1(vec2 p, float salt) {
    return fract(sin(dot(p + salt * 17.31, vec2(41.53, 289.1))) * 27183.7591);
}

void onb(vec3 n, out vec3 t, out vec3 b) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float bb = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * bb, -s * n.x);
    b = vec3(bb, s + n.y * n.y * a, -n.y);
}

vec3 cosineSampleHemisphere(vec3 n, vec2 seed) {
    float u1 = random(seed);
    float u2 = random1(seed, 91.7);

    float r = sqrt(u1);
    float theta = 6.2831853 * u2;

    vec3 t, b;
    onb(n, t, b);

    return normalize(t * (r * cos(theta)) + b * (r * sin(theta)) + n * sqrt(max(0.0, 1.0 - u1)));
}

float ambientOcclusion(vec3 p, vec3 n) {
    float ao = 0.0;
    float weight = 1.0;

    for (int i = 1; i <= 8; i++) {
        float h = 0.025 + 0.1 * float(i);
        float d = map(p + n * h);
        ao += max(h - d, 0.0) * weight;
        weight *= 0.8;
    }

    return 1.0 - clamp(ao * 0.8, 0.0, 1.0);
}

float fresnel(vec3 rd, vec3 n, float f0) {
    float cosTheta = max(dot(-rd, n), 0.0);
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

float ggxD(float NoH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float denom = NoH2 * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom + 1e-6);
}

float ggxG1(float NoV, float k) {
    return NoV / (NoV * (1.0 - k) + k);
}

float ggxSpecular(vec3 n, vec3 v, vec3 l, float roughness, float f0) {
    vec3 h = normalize(v + l);
    float NoV = max(dot(n, v), 1e-4);
    float NoL = max(dot(n, l), 1e-4);
    float NoH = max(dot(n, h), 0.0);
    float VoH = max(dot(v, h), 0.0);

    float k = (roughness + 1.0);
    k = k * k / 8.0;

    float D = ggxD(NoH, roughness);
    float G = ggxG1(NoV, k) * ggxG1(NoL, k);
    float F = f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);

    return (D * G * F) / (4.0 * NoV * NoL + 1e-4);
}

float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);

    f = f * f * (3.0 - 2.0 * f);

    float n000 = random(i.xy + i.z * 57.0);
    float n100 = random(i.xy + vec2(1.0, 0.0) + i.z * 57.0);
    float n010 = random(i.xy + vec2(0.0, 1.0) + i.z * 57.0);
    float n110 = random(i.xy + vec2(1.0, 1.0) + i.z * 57.0);
    float n001 = random(i.xy + (i.z + 1.0) * 57.0);
    float n101 = random(i.xy + vec2(1.0, 0.0) + (i.z + 1.0) * 57.0);
    float n011 = random(i.xy + vec2(0.0, 1.0) + (i.z + 1.0) * 57.0);
    float n111 = random(i.xy + vec2(1.0, 1.0) + (i.z + 1.0) * 57.0);

    return mix(
        mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
        mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y),
        f.z
    );
}

float fbm(vec3 p) {
    float v = 0.0;
    float amp = 0.2;

    for (int i = 0; i < 4; i++) {
        v += amp * noise3(p);
        p *= 2.0;
        amp *= 0.5;
    }

    return v;
}

vec3 ray(vec3 ro, vec3 rd, vec2 seed) {
    vec3 color = vec3(0.0);
    vec3 throughput = vec3(1.0);

    for (int bounce = 0; bounce < MAX_BOUNCES; bounce++) {
        float t = 0.0;

        for (int i = 0; i < 100; i++) {
            vec3 p = ro + rd * t;
            vec2 hit = mapMaterial(p);
            float d = hit.x;
            float ad = abs(d);
            if (ad < 0.001) {
                vec3 normal = getNormal(p);
                vec3 viewDir = -rd;

                vec3 lightCenter = vec3(0.0, 3, -1.0);
                vec3 lightHalfExtents = vec3(1.0, 0.0, 0.5);

                vec3 lightPos = lightCenter
                    + (random(seed) - 0.5) * 2.0 * vec3(lightHalfExtents.x, 0.0, 0.0)
                    + (random(seed + 50.0) - 0.5) * 2.0 * vec3(0.0, 0.0, lightHalfExtents.z);
                vec3 light = normalize(lightPos - p);
                float lightDist = length(lightPos - p);

                float diffuse = max(dot(light, normal), 0.0);
                diffuse /= 1.0 + lightDist * lightDist * 0.05;

                float s = shadow(p, light, lightDist);
                float ao = ambientOcclusion(p, normal);

                vec2 material = hit;
                vec3 surfaceColor;

                // glass
                if (material.y == 0.0) {
                    float eta = 1.0 / 2.0;
                    vec3 n2 = normal;
                    if (dot(rd, normal) > 0.0) {
                        n2 = -normal;
                        eta = 2.0;
                    }

                    float f = fresnel(rd, n2, 0.04);

                    if (random(p.xz + seed + float(bounce) * 7.0) < f) {
                        rd = reflect(rd, n2);
                        ro = p + n2 * 0.001;
                    } else {
                        vec3 refracted = refract(rd, n2, eta);
                        bool tir = dot(refracted, refracted) < 0.0001;
                        rd = tir ? reflect(rd, n2) : refracted;
                        ro = p + rd * 0.01;
                    }
                    break;
                }
                if (material.y == 1.0) {
                    rd = reflect(rd, normal);
                    ro = p + normal * 0.001;

                    break;
                }
                if (material.y == 2.0) {
                    float checker = mod(floor(p.x) + floor(p.z), 2.0);
                    float grout = fbm(p * 8.0);
                    surfaceColor = mix(vec3(0.85), vec3(0.1), checker);
                    surfaceColor *= 0.85 + 0.3 * grout;

                    float fFloor = fresnel(rd, normal, 0.04);
                    if (random(p.xz + seed + float(bounce) * 3.3) < fFloor * 0.5) {
                        vec3 jitter = (vec3(random(seed + 400.0), random(seed + 500.0), random(seed + 600.0)) - 0.5) * 0.04;
                        rd = normalize(reflect(rd, normal) + jitter);
                        ro = p + normal * 0.001;
                        throughput *= vec3(0.97);
                        continue;
                    }
                } else if (material.y == 3.0) {
                    float n = fbm(p * 1.0);
                    float n2 = fbm(p * 6.0);
                    surfaceColor = vec3(0.6745, 0.1529, 0.1529) * (0.6 + 0.3 * n + 0.15 * n2);
                } else if (material.y == 4.0) {
                    float n = fbm(p * 1.0);
                    float n2 = fbm(p * 6.0);
                    surfaceColor = vec3(0.05) * (0.6 + 0.3 * n + 0.15 * n2);
                } else if (material.y == 5.0) {
                    float n = fbm(p * 1.0);
                    float n2 = fbm(p * 6.0);
                    surfaceColor = vec3(0.2, 0.4, 1.0) * (0.6 + 0.3 * n + 0.15 * n2);
                } else if (material.y == 6.0) {
                    float n = fbm(p * 1.0);
                    float n2 = fbm(p * 6.0);
                    surfaceColor = vec3(0.2941, 1.0, 0.2) * (0.6 + 0.3 * n + 0.15 * n2);
                } else if (material.y == 7.0) {
                    float n = fbm(p * 1.0);
                    float n2 = fbm(p * 6.0);
                    surfaceColor = vec3(1.0) * (0.95 + 0.3 * n + 0.15 * n2);
                } else if (material.y == 8.0) {
                    surfaceColor = vec3(1.0);
                } else if (material.y == 9.0) {
                    vec3 emission = vec3(4.0);
                    color += throughput * emission;
                    return color;
                }

                vec3 lightColor = vec3(1.1, 1.0, 0.9);

                float roughness = (material.y == 2.0) ? 0.15 : 0.45;
                float spec = ggxSpecular(normal, viewDir, light, roughness, 0.04);

                color += throughput * (surfaceColor * diffuse * lightColor * s * ao
                                        + vec3(spec) * lightColor * diffuse * s);

                float bleedLuma = dot(surfaceColor, vec3(0.299, 0.587, 0.114));
                vec3 bleedColor = mix(vec3(bleedLuma), surfaceColor, COLOR_BLEED_SATURATION);
                throughput *= bleedColor * COLOR_BLEED_STRENGTH;

                rd = cosineSampleHemisphere(normal, p.xz + seed + float(bounce) * 3.71);
                ro = p + normal * 0.001;

                if (bounce >= RR_START_BOUNCE) {
                    float pSurvive = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 1.0);
                    if (random1(seed, float(bounce) * 13.1) > pSurvive) {
                        break;
                    }
                    throughput /= pSurvive;
                }

                break;
            }

            if (t > 100.0) {
                color += throughput * sky(rd);
                return color;
            }

            t += ad;
        }

        if (t >= 100.0) {
            color += throughput * sky(rd);
            return color;
        }
    }

    return color;
}

void main() {
    vec2 seed = gl_FragCoord.xy + float(u_frame) * 13.37 + fract(u_time) * 0.001;

    vec2 offset = vec2(random(seed), random(seed + 100.0)) - 0.5;
    vec2 sampleUV = ((gl_FragCoord.xy + offset) * 2.0 - u_resolution.xy) / u_resolution.y;

    vec3 rd = normalize(u_forward * u_fov + u_right * sampleUV.x + u_up * sampleUV.y);

    vec3 col = ray(u_ro, rd, seed);

    finalColor = vec4(col, 1.0);
}