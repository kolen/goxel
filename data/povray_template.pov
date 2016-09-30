// Generated from goxel {{version}}
// https://github.com/guillaumechereau/goxel

#default {
    finish {
        {{#mat}}
        ambient {{ambient}}
        diffuse {{diffuse}}
        specular {{specular}}
        roughness {{roughness}}
        {{/mat}}
    }
}

{{#camera}}
camera {
    perspective
    right x*{{width}}/{{height}}
    direction <0, 0, -1>
    angle {{angle}}
    transform {
        matrix {{modelview}}
        inverse
    }
}
{{/camera}}

#declare Voxel = box {<-0.5, -0.5, -0.5>, <0.5, 0.5, 0.5>}
#macro Vox(Pos, Color)
    object {
        Voxel
        translate Pos
        translate <0.5, 0.5, 0.5>
        texture { pigment {color rgb Color / 255} }
    }
#end

{{#light}}
global_settings {
    assumed_gamma 2.2
}

light_source {
    <0, 0, 1024> color rgb <1, 1, 1>
    parallel
    point_at {{point_at}}
}
{{/light}}

union {
{{#voxels}}
    Vox({{pos}}, {{color}})
{{/voxels}}
}
